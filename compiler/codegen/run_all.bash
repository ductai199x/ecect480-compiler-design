BT=$1
TARGET=$(echo $BT | cut -d'.' -f 1)

[ -z $BT ] && exit

make && 
./codegen "$BT"  "./out/$TARGET.bc" && 
./bc_compile.bash "out/$TARGET.bc" && 
"./out/$TARGET"