FILE=pid.txt
if [ -f "$FILE" ]; then
    OSD_PID=$(cat $FILE)
    sudo kill -s USR1 $OSD_PID
    echo "signal sent to pid $OSD_PID."
else 
    echo "$FILE does not exist."
fi
