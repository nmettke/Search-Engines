delete_data=false

while getopts "d" opt; do
    case "$opt" in
        d) delete_data=true ;;
    esac
done

if [ "$delete_data" = true ]; then
    git pull
    git reset --hard origin/main
    rm -rf data
    rm src/crawler/*.dat
fi

mkdir data
mkdir data/body_index
mkdir data/anchor_index
mkdir data/meta

source .env

mkdir build 
cd build
cmake ..
cd ../
cmake --build build
