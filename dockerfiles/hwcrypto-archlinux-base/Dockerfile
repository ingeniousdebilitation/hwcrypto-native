FROM base/archlinux
LABEL maintainer "arti.zirk@gmail.com"
RUN pacman --noconfirm -Syu base-devel git && pacman --noconfirm -Scc && echo "%wheel ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers
