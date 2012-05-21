INST=hdrs
DEST=/usr/include

make rmproper
make headers_check
make INSTALL_HDR_PATH=${INST}  headers_install
find ${INST}/include \( -name .install -o -name ..install.cmd \) -delete
sudo cp -rv ${INST}/include/* ${DEST}
rm -rf ${INST}

