#!/bin/bash

usage() {
    echo "Usage: $0 -p <path> -c <command>"
    echo "Commands: listAll, listMonitored, listStopped, purge"
    exit 1
}

# Initialize variables
declare -A monitored
declare -A targets
declare -A last_sync
declare -A results

parse_log() {
    local logfile="$1"
    while IFS= read -r line; do
        if [[ "$line" =~ \[([0-9]{4}-[0-9]{2}-[0-9]{2}\ [0-9]{2}:[0-9]{2}:[0-9]{2})\]\ Added\ directory:\ (.*)\ -\>\ (.*) ]]; then
            local timestamp="${BASH_REMATCH[1]}"
            local src="${BASH_REMATCH[2]}"
            local tgt="${BASH_REMATCH[3]}"
            monitored["$src"]=1
            targets["$src"]="$tgt"
        elif [[ "$line" =~ \[([0-9]{4}-[0-9]{2}-[0-9]{2}\ [0-9]{2}:[0-9]{2}:[0-9]{2})\]\ Monitoring\ stopped\ for\ (.*) ]]; then
            local src="${BASH_REMATCH[2]}"
            monitored["$src"]=0
        elif [[ "$line" =~ \[([0-9]{4}-[0-9]{2}-[0-9]{2}\ [0-9]{2}:[0-9]{2}:[0-9]{2})\]\ \[(.*)\]\ \[(.*)\]\ \[[0-9]+\]\ \[(.*)\]\ \[(.*)\]\ \[(.*)\] ]]; then
            local timestamp="${BASH_REMATCH[1]}"
            local src="${BASH_REMATCH[2]}"
            local tgt="${BASH_REMATCH[3]}"
            local result="${BASH_REMATCH[5]}"
            last_sync["$src"]="$timestamp"
            results["$src"]="$result"
        fi
    done < "$logfile"
}

# Check arguments
while getopts "p:c:" opt; do
    case "$opt" in
        p) path=$OPTARG ;;
        c) cmd=$OPTARG ;;
        *) usage ;;
    esac
done

if [[ -z "$path" || -z "$cmd" ]]; then
    usage
fi

case "$cmd" in
    listAll|listMonitored|listStopped)
        if [[ ! -f "$path" ]]; then
            echo "Error: $path is not a valid log file." >&2
            exit 1
        fi
        parse_log "$path"
        case "$cmd" in
            listAll)
                for src in "${!targets[@]}"; do
                    tgt="${targets[$src]}"
                    sync_time="${last_sync[$src]:-N/A}"
                    res="${results[$src]:-UNKNOWN}"
                    echo "$src -> $tgt [Last Sync: $sync_time] [$res]"
                done | sort
                ;;
            listMonitored)
                for src in "${!monitored[@]}"; do
                    if [[ "${monitored[$src]}" -eq 1 ]]; then
                        tgt="${targets[$src]}"
                        sync_time="${last_sync[$src]:-N/A}"
                        echo "$src -> $tgt [Last Sync: $sync_time]"
                    fi
                done | sort
                ;;
            listStopped)
                for src in "${!monitored[@]}"; do
                    if [[ "${monitored[$src]}" -eq 0 ]]; then
                        tgt="${targets[$src]}"
                        sync_time="${last_sync[$src]:-N/A}"
                        echo "$src -> $tgt [Last Sync: $sync_time]"
                    fi
                done | sort
                ;;
        esac
        ;;
    purge)
        if [[ -d "$path" ]]; then
            echo "Deleting directory $path..."
            if rm -rf "$path"; then
                echo "Purge complete."
            else
                echo "Error: Failed to delete directory." >&2
                exit 1
            fi
        elif [[ -f "$path" ]]; then
            echo "Deleting file $path..."
            if rm -f "$path"; then
                echo "Purge complete."
            else
                echo "Error: Failed to delete file." >&2
                exit 1
            fi
        else
            echo "Error: Path $path does not exist." >&2
            exit 1
        fi
        ;;
    *)
        echo "Invalid command: $cmd" >&2
        usage
        ;;
esac

exit 0