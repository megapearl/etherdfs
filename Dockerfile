FROM alpine:latest

RUN apk add --no-cache gcc make musl-dev linux-headers

COPY . /opt/etherdfs
WORKDIR /opt/etherdfs

# FIX: Verwijder de dubbele definitie in de broncode (conflicterende header uitschakelen)
RUN sed -i 's/#include <linux\/if_ether.h>/\/\/ #include <linux\/if_ether.h>/' ethersrv-linux.c

# Build en installatie
RUN make && cp ethersrv-linux /usr/local/bin/ && chmod +x /usr/local/bin/ethersrv-linux

# Opruimen
RUN apk del gcc make musl-dev linux-headers && rm -rf /opt/etherdfs

RUN mkdir -p /data
VOLUME /data

CMD ["/usr/local/bin/ethersrv-linux"]
