#!/bin/sh

PKGNAME=nfcollect
PKGVER=0.2
PKGREL=3
PKGDIR=${PKGNAME}-${PKGVER}
PKG="${PKGNAME}-${PKGVER}-${PKGREL}.tar.gz"

echo "Packaging $PKG ..."
echo

mkdir -p "${PKGDIR}"
cp -a bin lib include \
      configure configure.ac build-aux \
      Makefile.{in,am} \
      service \
      "${PKGDIR}"

tar --exclude "*.swp" \
    --exclude "*.o" \
    --exclude .deps \
    --exclude .dirstamp \
    -zcvf "$PKG" "${PKGDIR}"

SHA1=$(sha1sum "$PKG" | cut -f1 -d' ')
echo
echo "The SHA1 sum of $PKG is $SHA1"
