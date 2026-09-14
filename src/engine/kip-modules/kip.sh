# kip: the KIP helper Kai injects into native shell commands with KIP on (POSIX shells).
# The same verbs as `kai kip`, with no kai binary and no jq: POSIX sh + awk (mawk, gawk, busybox...).
#   kip hello --title Deploy
#   kip prompt --id p --field text name Name --required
#   kip recv                          # next answer from Kai into $KIP_MSG (exits 130 on Cancel)
#   name=$(kip get "$KIP_MSG" values.name)
# @@AWK@@ is replaced by kip.awk when Kai builds the command.
_KIP='@@AWK@@'
_kip_h=
kip() {
  case "${1-}" in
    hello|prompt|confirm|patch|invalid|message|markdown|progress|steps|step|table|notify|set-env|done|chip-result)
      # The first protocol message must be `hello`: send a plain one if the script did not.
      if [ -z "$_kip_h" ] && [ "$1" != hello ]; then printf '%s\n' '{"kip":1,"type":"hello"}'; fi
      _kip_h=1
      _kip_l=$(LC_ALL=C awk "$_KIP" "$@") || { _kip_r=$?; unset _kip_l; return "$_kip_r"; }
      printf '%s\n' "$_kip_l"; unset _kip_l ;;
    get)
      [ "$#" -eq 3 ] || { echo "Usage: kip get '<json>' <path>" >&2; return 2; }
      LC_ALL=C awk "$_KIP" get "$2" "$3" ;;
    recv)
      IFS= read -r KIP_MSG || exit 130
      case $KIP_MSG in *'"type":"cancel"'*) exit 130 ;; esac ;;
    raw)
      [ "$#" -eq 2 ] || { echo "Usage: kip raw '<json>'" >&2; return 2; }
      _kip_t=$(LC_ALL=C awk "$_KIP" get "$2" type 2>/dev/null) && LC_ALL=C awk "$_KIP" get "$2" kip >/dev/null 2>&1 || {
        echo 'Not a valid KIP message: it must be a JSON object with a "kip" key' >&2; unset _kip_t; return 2; }
      case $_kip_t in
        hello|prompt|confirm|patch|invalid|message|markdown|progress|steps|step|table|notify|set_env|done|chip_result) LC_ALL=C awk "$_KIP" get "$2" "" ;;
        *) echo "Not a valid KIP message: unknown message type '$_kip_t'" >&2; unset _kip_t; return 2 ;;
      esac
      unset _kip_t ;;
    help|--help|-h)
      echo "kip <verb> [options] - verbs: hello prompt confirm patch invalid message markdown progress steps step table notify set-env done chip-result raw get recv. Same syntax as kai kip; see Help > Creation manifesto." ;;
    '') echo "Usage: kip <verb> [options]   (kip --help)" >&2; return 2 ;;
    *) echo "Unknown verb '$1' (kip --help)" >&2; return 2 ;;
  esac
}
