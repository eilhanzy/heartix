REMOTE_ARCHIVE="https://github.com/htop-dev/htop/releases/download/3.2.2/htop-3.2.2.tar.xz"
ARCHIVE="htop-3.2.2.tar.xz"
UNPACKED_DIR="htop-3.2.2"

port_unpack() {
    tar xf "$ARCHIVE"
}

port_install() {
if [ -z "$PKG_CONFIG" ]; then
	export PKG_CONFIG=$TARGET-pkg-config.sh
fi
	CFLAGS="-D_DEFAULT_SOURCE=1" LDFLAGS="-static" ../$UNPACKED_DIR/configure --host=$TARGET --prefix=$PREFIX \
		--disable-hwloc --disable-unicode --enable-static --disable-shared
	make -j8
	make DESTDIR=$SYSROOT install
}
