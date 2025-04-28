#!/bin/bash

# Function to print usage instructions
usage() {
    echo "Usage: $0 -p <path> -c <command>"
    echo "Commands: listAll, listMonitored, listStopped, purge"
    exit 1
}

# Initialize variables
declare -A monitored # defines whether a source directory is currently monitored or stopped
declare -A dir # Maps a source directory to its corresponding target directory
declare -A last_sync # Stores the last synchronization timestamp for each source dir
declare -A results # Stores the result/status of the last sync

# Function to read given logfile line by line and store the stats in the corresponding arrays
parse_log() {
    local logfile="$1"
    while IFS= read -r line; do # read logfile line by line storing each line in "line" variable
        if [[ "$line" =~ \[(.*)\]\ \[(.*)\]\ \[(.*)\]\ \[(.*)\]\ \[(.*)\]\ \[(.*)\]\ \[(.*)\] ]]; then
			# Process worker report lines
            local timestamp="${BASH_REMATCH[1]}" # store timestamp
            local src="${BASH_REMATCH[2]}" # store source dir
            local tgt="${BASH_REMATCH[3]}" # store target dir
            local result="${BASH_REMATCH[5]}" # store the status of the sync
			dir["$src"]="$tgt" # Map target to source
			monitored["$src"]=1 # mark source as monitored
            last_sync["$src"]="$timestamp" # store sync time
            results["$src"]="$result" # store status of sync
        elif [[ "$line" =~ \[(.*)\]\ Monitoring\ stopped\ for\ (.*) ]]; then
			# Process monitor stopped lines
            local src="${BASH_REMATCH[2]}"
            monitored["$src"]=0 # mark source as not watched
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

if [[ -z "$path" || -z "$cmd" ]]; then # If either path or command is missing, show usage
    usage
fi

case "$cmd" in
    listAll | listMonitored | listStopped)
        if [[ ! -f "$path" ]]; then # if given path is not a file exit
            echo "Error: $path is not a valid log file." >&2
            exit 1
        fi
        parse_log "$path"
        case "$cmd" in
            listAll)
                for src in "${!dir[@]}"; do # search through every source directory we stored in dir
					# print the corresponding message
                    echo "$src -> ${dir[$src]} [Last Sync: ${last_sync[$src]:-N/A}] [${results[$src]:-UNKNOWN}]"
                done | sort # sort alphabetically
                ;;
            listMonitored)
                for src in "${!monitored[@]}"; do
                    if [[ "${monitored[$src]}" -eq 1 ]]; then # if source is monitored
                        echo "$src -> ${dir[$src]} [Last Sync: ${last_sync[$src]:-N/A}]" # print the corresponding message
                    fi
                done | sort
                ;;
            listStopped)
                for src in "${!monitored[@]}"; do
                    if [[ "${monitored[$src]}" -eq 0 ]]; then # if source is not monitored
                        echo "$src -> ${dir[$src]} [Last Sync: ${last_sync[$src]:-N/A}]" # print the corresponding message
                    fi
                done | sort
                ;;
        esac
        ;;
    purge)
        if [[ -d "$path" ]]; then # for purge if path is a target directory
            echo "Deleting $path..."
            if rm -rf "$path"; then # delete the dir recursively and print success or failure message
                echo "Purge complete."
            else
                echo "Error: Failed to delete directory." >&2
                exit 1
            fi
        elif [[ -f "$path" ]]; then # if path is a log file
            echo "Deleting $path..."
            if rm -f "$path"; then # just delete the file and print success of failure message
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