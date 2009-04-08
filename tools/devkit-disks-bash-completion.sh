
# Check for bash
[ -z "$BASH_VERSION" ] && return

####################################################################################################

__devkit_disks() {
    local IFS=$'\n'
    local cur="${COMP_WORDS[COMP_CWORD]}"

    if [ "${COMP_WORDS[$(($COMP_CWORD - 1))]}" = "--show-info" ] ; then
        COMPREPLY=($(compgen -W "$(devkit-disks --enumerate-device-files)" -- $cur))
    elif [ "${COMP_WORDS[$(($COMP_CWORD - 1))]}" = "--inhibit-polling" ] ; then
        COMPREPLY=($(compgen -W "$(devkit-disks --enumerate-device-files)" -- $cur))
    elif [ "${COMP_WORDS[$(($COMP_CWORD - 1))]}" = "--mount" ] ; then
        COMPREPLY=($(compgen -W "$(devkit-disks --enumerate-device-files)" -- $cur))
    elif [ "${COMP_WORDS[$(($COMP_CWORD - 1))]}" = "--unmount" ] ; then
        COMPREPLY=($(compgen -W "$(devkit-disks --enumerate-device-files)" -- $cur))
    elif [ "${COMP_WORDS[$(($COMP_CWORD - 1))]}" = "--ata-smart-refresh" ] ; then
        COMPREPLY=($(compgen -W "$(devkit-disks --enumerate-device-files)" -- $cur))
    elif [ "${COMP_WORDS[$(($COMP_CWORD - 1))]}" = "--ata-smart-simulate" ] ; then
        _filedir || return 0
    else
        COMPREPLY=($(IFS=: compgen -W "--dump:--inhibit-polling:--inhibit-all-polling:--enumerate:--enumerate-device-files:--monitor:--monitor-detail:--show-info:--help:--mount:--mount-fstype:--mount-options:--unmount:--unmount-options:--ata-smart-refresh:--ata-smart-wakeup:--ata-smart-simulate" -- $cur))
    fi
}

####################################################################################################

complete -o filenames -F __devkit_disks devkit-disks
