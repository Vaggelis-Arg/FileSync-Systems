#!/bin/bash

usage() {
    echo "Usage: $0 -p <path> -c <command>"
    echo "Commands: listAll, listMonitored, listStopped, purge"
    exit 1
}

# Associative arrays to track sync data
declare -A monitored
declare -A dir
declare -A last_sync
declare -A results

# Parse the log file and populate tracking arrays
parse_log() {
    local logfile="$1"
    while IFS= read -r line; do
        if [[ "$line" =~ \[(.*)\]\ \[(.*)\]\ \[(.*)\]\ \[(.*)\]\ \[(.*)\]\ \[(.*)\]\ \[(.*)\] ]]; then
            local timestamp="${BASH_REMATCH[1]}"
            local src="${BASH_REMATCH[2]}"
            local tgt="${BASH_REMATCH[3]}"
            local result="${BASH_REMATCH[5]}"
			dir["$src"]="$tgt"
			monitored["$src"]=1
            last_sync["$src"]="$timestamp"
            results["$src"]="$result"
        elif [[ "$line" =~ \[(.*)\]\ Monitoring\ stopped\ for\ (.*) ]]; then
            local src="${BASH_REMATCH[2]}"
            monitored["$src"]=0
        fi
    done < "$logfile"
}

# Check arguments
while getopts "p:c:" opt; do
    case "$opt" in
        p) path=$OPTARG ;; # p --> path argument
        c) cmd=$OPTARG ;; # c --> command argument
        *) usage ;; # other options --> call usage function to display manual
    esac
done

if [[ -z "$path" || -z "$cmd" ]]; then
    usage
fi

# Execute the specified command
case "$cmd" in
    listAll | listMonitored | listStopped)
        if [[ ! -f "$path" ]]; then
            echo "Error: $path is not a valid log file." >&2
            exit 1
        fi
        parse_log "$path"
        case "$cmd" in
            listAll)
                for src in "${!dir[@]}"; do
                    echo "$src -> ${dir[$src]} [Last Sync: ${last_sync[$src]:-N/A}] [${results[$src]:-UNKNOWN}]"
                done | sort
                ;;
            listMonitored)
                for src in "${!monitored[@]}"; do
                    if [[ "${monitored[$src]}" -eq 1 ]]; then
                        echo "$src -> ${dir[$src]} [Last Sync: ${last_sync[$src]:-N/A}]"
                    fi
                done | sort
                ;;
            listStopped)
                for src in "${!monitored[@]}"; do
                    if [[ "${monitored[$src]}" -eq 0 ]]; then
                        echo "$src -> ${dir[$src]} [Last Sync: ${last_sync[$src]:-N/A}]"
                    fi
                done | sort
                ;;
        esac
        ;;
    purge)
		# Delete file or directory at the specified path
        if [[ -d "$path" ]]; then
            echo "Deleting $path..."
            if rm -rf "$path"; then
                echo "Purge complete."
            else
                echo "Error: Failed to delete directory." >&2
                exit 1
            fi
        elif [[ -f "$path" ]]; then
            echo "Deleting $path..."
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