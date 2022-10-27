if [ "$1" = "-d" ] ; then
    g++ debug.c ./EV3_RobotControl/btcomm.c -lbluetooth -o debug
elif [ "$1" = "" ] ; then
    g++ EV3_Localization.c -g ./EV3_RobotControl/btcomm.c -lbluetooth  -o localisation
else
    g++ $1.c ./EV3_RobotControl/btcomm.c -lbluetooth  -o $1
fi