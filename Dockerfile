# Этап 1: сборка llama.cpp с поддержкой AVX2 (если нужна максимальная скорость на современных CPU)
FROM python:3.11-slim AS builder

# Установка системных зависимостей
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Клонирование и сборка llama.cpp
WORKDIR /opt
RUN git clone https://github.com/ggerganov/llama.cpp && \
    cd llama.cpp && \
    make -j$(nproc) LLAMA_AVX2=1 LLAMA_NATIVE=0

# Этап 2: финальный образ
FROM python:3.11-slim

# Установка только необходимых зависимостей
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    libgomp1 \
    && rm -rf /var/lib/apt/lists/*

# Создание пользователя без root-прав
RUN useradd --create-home --shell /bin/bash botuser
USER botuser
WORKDIR /home/botuser

# Копируем бинарники llama.cpp
COPY --from=builder --chown=botuser:botuser /opt/llama.cpp/llama-cli /usr/local/bin/
COPY --from=builder --chown=botuser:botuser /opt/llama.cpp/libllama.so* /usr/local/lib/

# Копируем код и модель (модель — отдельно, см. .dockerignore!)
COPY --chown=botuser:botuser . .

# Проверка приватности токена
RUN chmod 600 .token

# Запуск
CMD ["python", "group.py"]