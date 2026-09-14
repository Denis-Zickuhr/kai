<?php
// kip - Kai Interface Protocol for PHP (provided by Kai to KIP commands).
//
//   $values = Kip::prompt([['name' => 'env', 'type' => 'select', 'options' => ['dev', 'prod']]],
//                         ['id' => 'where', 'title' => 'Where to?']);
//   Kip::progress(50, 'Deploying...');
//   Kip::done('Deployed ' . $values['env']);
//
// `Kip` is a class available everywhere in the command (no require needed). Outside Kai's own
// inline code, a script file finds it through KAI_MODULES (the folder Kai fills) too:
// require getenv('KAI_MODULES') . '/kip.php';
// `hello` is sent before the first message. Cancel exits the process with code 130; the Back
// button throws KipBack. Options are plain arrays with the protocol's own keys (submit_label,
// confirm_label...). PHP cannot tell [] from {}: send (object)[] when you need an empty object.
// Full protocol: docs/kip.md.
if (!class_exists('Kip', false)) {

class KipBack extends Exception
{
    public function __construct()
    {
        parent::__construct('back');
    }
}

class Kip
{
    const VERSION = 1;

    private static $started = false;
    private static $in = null;
    private static $out = null;
    private static $err = null;

    private static function write($msg)
    {
        if (self::$out === null) {
            self::$out = fopen('php://stdout', 'w');
        }
        $flags = JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES | JSON_INVALID_UTF8_SUBSTITUTE;
        fwrite(self::$out, json_encode($msg, $flags) . "\n");
        fflush(self::$out);
    }

    // Send any protocol message: Kip::send('message', ['level' => 'info', 'text' => 'hi']).
    public static function send($type, $fields = [])
    {
        if ($type !== 'hello' && !self::$started) {
            self::hello();
        }
        if ($type === 'hello') {
            self::$started = true;
        }
        $msg = ['kip' => self::VERSION, 'type' => $type];
        foreach ($fields as $key => $value) {
            if ($value !== null) {
                $msg[$key] = $value;
            }
        }
        self::write($msg);
    }

    public static function hello($title = null, $version = null)
    {
        self::send('hello', ['title' => $title, 'version' => $version]);
    }

    // Free-form log line (shown in Details > Log).
    public static function log(...$parts)
    {
        if (self::$err === null) {
            self::$err = fopen('php://stderr', 'w');
        }
        $texts = [];
        foreach ($parts as $part) {
            $texts[] = is_scalar($part) || $part === null ? (string) $part : json_encode($part);
        }
        fwrite(self::$err, implode(' ', $texts) . "\n");
    }

    private static function read()
    {
        if (self::$in === null) {
            self::$in = fopen('php://stdin', 'r');
        }
        for (;;) {
            $line = fgets(self::$in);
            if ($line === false) {
                exit(130); // Kai closed our stdin: the run is over
            }
            $msg = json_decode($line, true);
            if (!is_array($msg)) {
                continue;
            }
            if (($msg['type'] ?? null) === 'cancel') {
                exit(130);
            }
            return $msg;
        }
    }

    private static function isPatch($reply)
    {
        return is_array($reply)
            && (array_key_exists('fields', $reply) || array_key_exists('remove', $reply) || array_key_exists('chips', $reply));
    }

    // Wait for the answer to prompt/confirm `$id`, serving change/chip/invalid on the way.
    private static function answer($id, $callbacks = [])
    {
        $onChange = $callbacks['onChange'] ?? null;
        $onChip = $callbacks['onChip'] ?? null;
        $validate = $callbacks['validate'] ?? null;
        for (;;) {
            $msg = self::read();
            if (($msg['id'] ?? null) !== $id) {
                continue;
            }
            $kind = $msg['type'] ?? null;
            if ($kind === 'response') {
                $values = $msg['values'] ?? [];
                $errors = $validate ? $validate($values) : null;
                if ($errors) {
                    self::send('invalid', ['id' => $id, 'errors' => $errors]);
                    continue;
                }
                return $values;
            }
            if ($kind === 'back') {
                throw new KipBack();
            }
            if ($kind === 'change') {
                $reply = $onChange ? $onChange($msg['field'] ?? null, $msg['values'] ?? []) : null;
                $patch = self::isPatch($reply) ? $reply : ['fields' => $reply ?: []];
                self::send('patch', array_merge(['id' => $id, 'seq' => $msg['seq'] ?? null], $patch));
            } elseif ($kind === 'chip' && $onChip) {
                $chip = $msg['chip'] ?? null;
                try {
                    $result = $onChip($chip, $msg['values'] ?? []);
                    $result = is_string($result) ? ['text' => $result] : (is_array($result) ? $result : []);
                    if (!isset($result['state'])) {
                        $result['state'] = 'success';
                    }
                } catch (Throwable $error) {
                    $result = ['state' => 'error', 'text' => $error->getMessage()];
                }
                self::send('chip_result', array_merge(['chip' => $chip, 'id' => $id], $result));
            }
        }
    }

    private static function split($options)
    {
        $callbacks = [];
        foreach (['onChange', 'onChip', 'validate'] as $name) {
            if (isset($options[$name])) {
                $callbacks[$name] = $options[$name];
            }
            unset($options[$name]);
        }
        return [$options, $callbacks];
    }

    // Show a form and return its values array.
    // options: id, title, description, submit_label, back, cancellable, remember, chips,
    //          onChange($field, $values) -> fields to patch (a `watch` field changed),
    //          onChip($chip, $values) -> text or [state, title, text] for a chip,
    //          validate($values) -> [field => message] to keep the form open.
    public static function prompt($fields, $options = [])
    {
        list($options, $callbacks) = self::split($options);
        $id = $options['id'] ?? 'prompt';
        self::send('prompt', array_merge(['id' => $id, 'fields' => $fields], $options));
        return self::answer($id, $callbacks);
    }

    // Yes/no screen; returns true/false. options: id, title, danger, confirm_label, cancel_label, back.
    public static function confirm($text, $options = [])
    {
        $id = $options['id'] ?? 'confirm';
        self::send('confirm', array_merge(['id' => $id, 'text' => $text], $options));
        $values = self::answer($id);
        return !empty($values['confirmed']);
    }

    public static function message($text, $level = 'info')
    {
        self::send('message', ['level' => $level, 'text' => $text]);
    }

    public static function markdown($text)
    {
        self::send('markdown', ['text' => $text]);
    }

    // $value 0-100, or null for an indeterminate bar.
    public static function progress($value, $label = null, $options = [])
    {
        if (!self::$started) {
            self::hello();
        }
        $msg = ['kip' => self::VERSION, 'type' => 'progress', 'value' => $value]; // null is meaningful here
        foreach (array_merge($options, ['label' => $label]) as $key => $item) {
            if ($item !== null) {
                $msg[$key] = $item;
            }
        }
        self::write($msg);
    }

    // $items: [['id' => 'build', 'label' => 'Build'], ...]
    public static function steps($id, $items, $title = null)
    {
        self::send('steps', ['id' => $id, 'title' => $title, 'items' => $items]);
    }

    // $state: pending | running | success | error | skipped
    public static function step($stepsId, $id, $state, $detail = null)
    {
        self::send('step', ['steps' => $stepsId, 'id' => $id, 'state' => $state, 'detail' => $detail]);
    }

    // $columns: ['name', ...] or [['key' => 'name', 'label' => 'Name'], ...]
    public static function table($columns, $rows, $options = [])
    {
        $cols = [];
        foreach ($columns as $column) {
            $cols[] = is_string($column) ? ['key' => $column, 'label' => $column] : $column;
        }
        self::send('table', array_merge($options, ['columns' => $cols, 'rows' => $rows]));
    }

    public static function notify($title, $text = null, $level = null)
    {
        self::send('notify', ['title' => $title, 'text' => $text, 'level' => $level]);
    }

    // Export a dynamic variable (the name must be in the command's Exportable variables).
    public static function setEnv($name, $value)
    {
        self::send('set_env', ['name' => $name, 'value' => $value]);
    }

    // Spontaneous patch of the open prompt `$id` (no `seq`): replace fields (by name), remove
    // fields and/or replace the whole chip set. For what the program decides on its own, e.g. a
    // chip that repaints a table; answers to `change` are sent by `onChange`.
    public static function patch($id, $fields = [], $remove = null, $chips = null)
    {
        self::send('patch', ['id' => $id, 'fields' => $fields, 'remove' => $remove, 'chips' => $chips]);
    }

    public static function chipResult($chip, $state = 'success', $text = null, $title = null, $id = null)
    {
        self::send('chip_result', ['chip' => $chip, 'state' => $state, 'text' => $text, 'title' => $title, 'id' => $id]);
    }

    // Result screen: Kip::done('Deployed', 'All good') or Kip::done(['title' => ..., 'actions' => [...]]).
    // actions: [['type' => 'open_url', 'label' => 'Open', 'url' => 'https://...']]
    public static function done($title = null, $text = null, $level = null, $actions = null)
    {
        if (is_array($title)) {
            self::send('done', $title);
            return;
        }
        self::send('done', ['title' => $title, 'text' => $text, 'level' => $level, 'actions' => $actions]);
    }
}

}
