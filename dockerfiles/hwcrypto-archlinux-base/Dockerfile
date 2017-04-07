FROM base/archlinux
LABEL maintainer "arti.zirk@gmail.com"
RUN pacman --noconfirm -Sy grep && pacman --noconfirm -Su base-devel git qt5-base qt5-tools pcsclite ccid && pacman --noconfirm -Scc && echo "%wheel ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers
