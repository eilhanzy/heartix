REMOTE_ARCHIVE="https://ftp.gnu.org/gnu/ncurses/ncurses-6.4.tar.gz"
ARCHIVE="ncurses-6.4.tar.gz"
UNPACKED_DIR="ncurses-6.4"

port_unpack() {
    tar xf "$ARCHIVE"
}

port_install() {
if [ -z "$PKG_CONFIG" ]; then
	export PKG_CONFIG=$TARGET-pkg-config.sh
fi
	CFLAGS="-DCAIRO_NO_MUTEX=1" ../$UNPACKED_DIR/configure --host=$TARGET --prefix=$PREFIX \
		--enable-xlib=no --enable-shared=yes --enable-static=no \
		--without-progs --without-tack --without-manpages \
		--without-tests \
		--with-default-terminfo-dir=$PREFIX/share/terminfo \
		--with-terminfo-dirs=$PREFIX/share/terminfo
	make -j8
	make DESTDIR=$SYSROOT install.libs install.includes
}
