#Don't run, kills program in current dev state
FILE=pid.txt
if [ -f "$FILE" ]; then
    OSD_PID=$(cat $FILE)
    sudo kill -s USR2 $OSD_PID
    echo "signal sent to pid $OSD_PID."
else 
    echo "$FILE does not exist."
fi
