#!/bin/bash
VENDOR=martinpaljak
DISTROS = debian-testing fedora-25 archlinux ubuntu-16.04 ubuntu-16.10 ubuntu-17.04

prepare:
	for f in $(DISTROS); do \
	  docker build -t hwcrypto-$$f-base hwcrypto-$$f-base; \
	  docker build -t hwcrypto-$$f-builder hwcrypto-$$f-builder; \
	done


tag:
	for f in $(DISTROS); do \
	  docker tag hwcrypto-$$f-base $(VENDOR)/hwcrypto-$$f-base; \
	  docker tag hwcrypto-$$f-builder $(VENDOR)/hwcrypto-$$f-builder; \
	done

push:
	for f in $(DISTROS); do \
	  docker push $(VENDOR)/hwcrypto-$$f-builder; \
	done

build:
	for f in $(DISTROS); do \
	  docker run -v $${PWD}:/artifacts $(VENDOR)/hwcrypto-$$f-builder; \
	done

clean:
	rm -f *.deb *.rpm *.pkg.tar.xz
