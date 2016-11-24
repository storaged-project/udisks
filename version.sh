#!/bin/bash

print_help()
{
    echo "Usage:"
    echo "  $(basename $0) [OPTION...]"
    echo
    echo "  -h, --help      Print this message"
    echo "  -v, --version   Print the version string"
    echo "  -r, --release   Print the release string"
    echo "  -f, --full      Print the version-release string"
}

parse_configure_ac()
{
    m4 -P << 'END'
m4_changequote(`[', `]')
m4_include(configure.ac)
udisks_version
END
}

get_release()
{
    git_hash=$(git show -s --pretty=%h 2> /dev/null)
    if [[ $? == 0 ]]; then
        now=$(date +"%Y%m%d")
        echo "0.${now}git${git_hash}"
    else
        echo "1"
    fi
}

version=$(parse_configure_ac | tail -1)
release=$(get_release)

case $1 in
    -h|--help)
        print_help
        exit 0
        ;;
    -v|--version)
        echo $version
        exit 0
        ;;
    -r|--release)
        echo $release
        exit 0
        ;;
    -f|--full)
        echo ${version}-${release}
        ;;
    *)
        print_help
        exit 1
        ;;
esac
