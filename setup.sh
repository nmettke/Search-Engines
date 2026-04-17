delete_data=false

while getopts "d" opt; do
    case "$opt" in
        d) delete_data=true ;;
    esac
done

if [ "$delete_data" = true ]; then
    rm -rf data
    rm src/crawler/*.dat
fi

mkdir data
mkdir data/body_index
mkdir data/anchor_index
mkdir data/meta
mkdir data/parsed_anchor_index/
mkdir data/parsed_meta
mkdir data/disk_chunk_backup

mkdir build 
cd build
cmake ..
cd ../
cmake --build build
