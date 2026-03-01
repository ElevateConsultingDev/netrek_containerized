# Netrek pygame development client
FROM python:3.11-slim-bullseye

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
    apt-get install -y \
    libsdl2-2.0-0 \
    libsdl2-image-2.0-0 \
    libsdl2-mixer-2.0-0 \
    libsdl2-ttf-2.0-0 && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY netrek_client_pygame/requirements.txt /app/requirements.txt
RUN pip install --no-cache-dir -r requirements.txt

COPY netrek_client_pygame/ /app/

COPY pygame-client/entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh

ENV DISPLAY=host.docker.internal:0

ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
