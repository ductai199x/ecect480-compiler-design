BT=$1
TARGET=$(echo $BT | cut -d'.' -f 1)
LBT="${TARGET}.linked.bc"
LL="${TARGET}.ll"
O="${TARGET}.o"

rm $BT
rm $LBT
rm $O
rm $LL
rm $TARGET
