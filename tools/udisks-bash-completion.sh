
# Check for bash
[ -z "$BASH_VERSION" ] && return

####################################################################################################

__udisks() {
    local IFS=$'\n'
    local cur="${COMP_WORDS[COMP_CWORD]}"

    if [ "${COMP_WORDS[$(($COMP_CWORD - 1))]}" = "--show-info" ] ; then
        COMPREPLY=($(compgen -W "$(udisks --enumerate-device-files)" -- $cur))
    elif [ "${COMP_WORDS[$(($COMP_CWORD - 1))]}" = "--inhibit-polling" ] ; then
        COMPREPLY=($(compgen -W "$(udisks --enumerate-device-files)" -- $cur))
    elif [ "${COMP_WORDS[$(($COMP_CWORD - 1))]}" = "--mount" ] ; then
        COMPREPLY=($(compgen -W "$(udisks --enumerate-device-files)" -- $cur))
    elif [ "${COMP_WORDS[$(($COMP_CWORD - 1))]}" = "--unmount" ] ; then
        COMPREPLY=($(compgen -W "$(udisks --enumerate-device-files)" -- $cur))
    elif [ "${COMP_WORDS[$(($COMP_CWORD - 1))]}" = "--detach" ] ; then
        COMPREPLY=($(compgen -W "$(udisks --enumerate-device-files)" -- $cur))
    elif [ "${COMP_WORDS[$(($COMP_CWORD - 1))]}" = "--ata-smart-refresh" ] ; then
        COMPREPLY=($(compgen -W "$(udisks --enumerate-device-files)" -- $cur))
    elif [ "${COMP_WORDS[$(($COMP_CWORD - 1))]}" = "--ata-smart-simulate" ] ; then
        _filedir || return 0
    elif [ "${COMP_WORDS[$(($COMP_CWORD - 1))]}" = "--set-spindown" ] ; then
        COMPREPLY=($(compgen -W "$(udisks --enumerate-device-files)" -- $cur))
    elif [ "${COMP_WORDS[$(($COMP_CWORD - 1))]}" = "--poll-for-media" ] ; then
        COMPREPLY=($(compgen -W "$(udisks --enumerate-device-files)" -- $cur))
    else
        COMPREPLY=($(IFS=: compgen -W "--dump:--inhibit-polling:--inhibit-all-polling:--enumerate:--enumerate-device-files:--monitor:--monitor-detail:--show-info:--help:--mount:--mount-fstype:--mount-options:--unmount:--unmount-options:--detach:--detach-options:--ata-smart-refresh:--ata-smart-wakeup:--ata-smart-simulate:--set-spindown:--set-spindown-all:--spindown-timeout:--poll-for-media" -- $cur))
    fi
}

####################################################################################################

complete -o filenames -F __udisks udisks
