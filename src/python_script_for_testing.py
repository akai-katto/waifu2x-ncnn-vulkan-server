import socket
import time

HOST = "localhost"  # Standard loopback interface address (localhost)
PORT = 3509  # Port to listen on (non-privileged ports are > 1023)

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    s.connect((HOST, PORT))

    raw_bytes = "{" \
                f"\"noise\": {3} ," \
                f" \"scale\": {2} ," \
                f" \"tilesize\": {200}," \
                f" \"prepadding\": {18}," \
                f" \"gpuid\": {0}," \
                " \"tta\": 0," \
                f" \"param_path\": \"models/models-cunet/noise3_scale2.0x_model.param\"," \
                f" \"model_path\": \"models/models-cunet/noise3_scale2.0x_model.bin\"" \
                "}"

    s.send(bytes(raw_bytes, "utf-8"))
    print(s.recv(1))
    s.send(b"1920")
    print(s.recv(1))
    s.send(b"1080")
    print(s.recv(1))

    with open("/Users/tyler/Desktop/VID_20220918_114308_00_238_2022-11-27_23-36-22_screenshot.jpg", "rb") as f:
        def divide_chunks(input_list, n):
            for i in range(0, len(input_list), n):
                yield input_list[i:i + n]
        chunks = divide_chunks(f.read(), 65536)

    for chunk in chunks:
        s.send(chunk)
        print(s.recv(1))

    s.send(b"done")
    s.recv(1)

time.sleep(10)
with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    s.connect((HOST, 3510))

    all_bytes = b""
    recv = b""
    while recv != b"done":
        recv = s.recv(65536)
        if recv != b"done":
            all_bytes += recv

    with open("output.bmp", "wb") as f:
        f.write(all_bytes)
