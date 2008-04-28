
# Check for bash
[ -z "$BASH_VERSION" ] && return

####################################################################################################

__devkit_disks() {
    local IFS=$'\n'
    local cur="${COMP_WORDS[COMP_CWORD]}"

    if [ "${COMP_WORDS[$(($COMP_CWORD - 1))]}" = "--show-info" ] ; then
        COMPREPLY=($(compgen -W "$(devkit-disks --enumerate)" -- $cur))
    elif [ "${COMP_WORDS[$(($COMP_CWORD - 1))]}" = "--mount" ] ; then
        COMPREPLY=($(compgen -W "$(devkit-disks --enumerate)" -- $cur))
    elif [ "${COMP_WORDS[$(($COMP_CWORD - 1))]}" = "--unmount" ] ; then
        COMPREPLY=($(compgen -W "$(devkit-disks --enumerate)" -- $cur))
    elif [ "${COMP_WORDS[$(($COMP_CWORD - 1))]}" = "--create-fs" ] ; then
        COMPREPLY=($(compgen -W "$(devkit-disks --enumerate)" -- $cur))
    else
        COMPREPLY=($(IFS=: compgen -S' ' -W "--inhibit:--enumerate:--monitor:--monitor-detail:--show-info:--help:--mount:--mount-fstype:--mount-options:--unmount:--unmount-options:--create-fs:--create-fs-type:--create-fs-options" -- $cur))
    fi
}

####################################################################################################

complete -o nospace -F __devkit_disks devkit-disks
