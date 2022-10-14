if [ "$1" = "-d" ] ; then
    g++ debug.c ./EV3_RobotControl/btcomm.c -lbluetooth -o debug
else
    echo "$1"
    g++ EV3_Localization.c ./EV3_RobotControl/btcomm.c -lbluetooth  -o localisation
fi