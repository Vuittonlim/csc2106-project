import usocket
import ustruct
import urandom
import utime

def send_coap_post(server_ip, port, path, payload, max_retries=4, timeout_ms=2000, verbose=False):
    """
    Sends a Confirmable (CON) CoAP POST request.
    Retries up to max_retries times if no ACK is received.
    Returns True if ACK received, False if all retries failed.
    """
    msg_id = urandom.getrandbits(16)

    # ── Build CoAP packet ─────────────────────────────────
    # Header: version=1, type=CON(0), tkl=0, code=POST(0.02)
    header = ustruct.pack('!BBH',
        (1 << 6) | (0 << 4) | 0,   # CON type = 0
        0x02,                        # POST
        msg_id
    )
    path_bytes = path.encode()
    option  = bytes([0xB0 | len(path_bytes)]) + path_bytes
    packet  = header + option + b'\xFF' + payload.encode()

    # ── Send with retry until ACK ─────────────────────────
    sock = None                      
    try:
        sock = usocket.socket(usocket.AF_INET, usocket.SOCK_DGRAM)
        sock.settimeout(timeout_ms / 1000)
        addr = usocket.getaddrinfo(server_ip, port)[0][-1]

        for attempt in range(max_retries):
            try:
                if verbose: print(f"CoAP sending attempt {attempt + 1}/{max_retries}...")
                sock.sendto(packet, addr)

                # wait for ACK from server
                response, _ = sock.recvfrom(256)

                # parse response header
                resp_type   = (response[0] >> 4) & 0x03
                resp_msg_id = ustruct.unpack('!H', response[2:4])[0]

                # ACK type = 2, must match our message ID
                if resp_type == 2 and resp_msg_id == msg_id:
                    if verbose: print("CoAP ACK received ✓")
                    return True
                else:
                    if verbose: print("Wrong ACK received, retrying...")

            except Exception as e:
                print(f"CoAP attempt {attempt + 1} failed: {e}")
                utime.sleep_ms(500)

        print("CoAP: all retries failed!")
        return False

    finally:
        if sock:                    
            sock.close()
