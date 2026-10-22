// termbridge: stand-in for "a Go binary that connects to a terminal and runs
// scripts there". It opens a pty, starts the payload on it in its OWN session
// (like a remote shell would be), and mirrors its output. On SIGINT it forwards
// ^C to the remote side, waits for the payload to finish cleaning up and exits
// 130. Any other signal (SIGTERM) kills it at once, leaving the payload alone.
package main

import (
	"fmt"
	"io"
	"os"
	"os/exec"
	"os/signal"
	"syscall"
	"unsafe"
)

func ioctl(fd uintptr, req uintptr, arg uintptr) error {
	if _, _, e := syscall.Syscall(syscall.SYS_IOCTL, fd, req, arg); e != 0 {
		return e
	}
	return nil
}

func openPty() (*os.File, *os.File, error) {
	m, err := os.OpenFile("/dev/ptmx", os.O_RDWR, 0)
	if err != nil {
		return nil, nil, err
	}
	var unlock int32
	if err := ioctl(m.Fd(), syscall.TIOCSPTLCK, uintptr(unsafe.Pointer(&unlock))); err != nil {
		return nil, nil, err
	}
	var n uint32
	if err := ioctl(m.Fd(), syscall.TIOCGPTN, uintptr(unsafe.Pointer(&n))); err != nil {
		return nil, nil, err
	}
	s, err := os.OpenFile(fmt.Sprintf("/dev/pts/%d", n), os.O_RDWR|syscall.O_NOCTTY, 0)
	if err != nil {
		return nil, nil, err
	}
	return m, s, nil
}

func main() {
	if len(os.Args) < 2 {
		fmt.Fprintln(os.Stderr, "usage: termbridge <payload-script> [args...]")
		os.Exit(2)
	}
	master, slave, err := openPty()
	if err != nil {
		fmt.Fprintln(os.Stderr, "pty:", err)
		os.Exit(1)
	}

	cmd := exec.Command("bash", os.Args[1:]...)
	cmd.Stdin, cmd.Stdout, cmd.Stderr = slave, slave, slave
	cmd.SysProcAttr = &syscall.SysProcAttr{Setsid: true, Setctty: true, Ctty: 0}
	if err := cmd.Start(); err != nil {
		fmt.Fprintln(os.Stderr, "start:", err)
		os.Exit(1)
	}
	slave.Close()
	fmt.Printf("[termbridge] connected, remote pid=%d\n", cmd.Process.Pid)

	go io.Copy(os.Stdout, master)

	done := make(chan error, 1)
	go func() { done <- cmd.Wait() }()

	sigs := make(chan os.Signal, 1)
	signal.Notify(sigs, syscall.SIGINT) // SIGTERM deliberately NOT handled
	interrupted := false
	for {
		select {
		case <-sigs:
			if !interrupted {
				interrupted = true
				fmt.Println("[termbridge] SIGINT -> forwarding ^C to the remote side")
				master.Write([]byte{0x03})
			}
		case <-done:
			if interrupted {
				fmt.Println("[termbridge] remote finished, exit 130")
				os.Exit(130)
			}
			fmt.Println("[termbridge] remote finished")
			return
		}
	}
}
