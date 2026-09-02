# Maintainer: FTEXX00 Linux driver contributors

pkgname=focaltech-ftexx00-dkms
pkgver=0.1
pkgrel=1
pkgdesc="DKMS SPI transport for FocalTech FTE3600/FTE4800/FTE6600/FTE6900 fingerprint readers"
arch=('any')
url="https://github.com/io-tl/fte3600-piccolo"
license=('GPL-2.0-only')
depends=('dkms')
optdepends=('linux-headers: build for the Arch Linux kernel'
            'linux-lts-headers: build for the Arch Linux LTS kernel')
provides=('focaltech-spi-dkms' 'focaltech-fte3600-piccolo-dkms')
conflicts=('focaltech-spi-dkms' 'focaltech-fte3600-piccolo-dkms')
source=('focal_spi.c' 'Makefile' 'dkms.conf' 'LICENSE')
sha256sums=('86b10f3105ab71b1200daaf4084788a99446767c196f4aba8823c893e4b4f5de'
            '4aac305dfcf0a31e87596461ae8f757d373213384cc85043aa16d8484c0d4702'
            'a2277c20e29925e24af7c38ed1b273f80102ef798b5b9f7779d888e1d6e4c6fd'
            'aaf135472f81c5b4a0dca9367e5bb5e9750032b5bebe5442b36e4c0a47430df3')

package() {
  local dkms_src="$pkgdir/usr/src/focaltech-ftexx00-$pkgver"

  install -Dm644 focal_spi.c "$dkms_src/focal_spi.c"
  install -Dm644 Makefile "$dkms_src/Makefile"
  install -Dm644 dkms.conf "$dkms_src/dkms.conf"
  install -Dm644 LICENSE \
    "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
