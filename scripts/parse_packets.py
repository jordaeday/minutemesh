from pprint import pprint

def parse_file(file_path):
    results = []
    # read a line at a time
    with open(file_path, 'r') as file:
        for line in file:
            packet = parse_line(line)
            if packet:
                results.append(packet)
    return results

def parse_line(line):
    fields = line.strip().split(',')
    type = fields[1]
    if type != 'RXLOG':
        return None
    packet = {
        'timestamp': fields[0],
        'type': fields[1],
        'rssi': fields[2],
        'snr': fields[3],
        'payload': process_lora(fields[4])
    }
    return packet

def process_lora(payload):
    # simple little-endian interpretation per field:
    # take the original character slice for each field (hex chars),
    # split into 2-char bytes and reverse the byte order.
    def rev_bytes(s):
        if not s:
            return ''
        s = str(s).strip()
        if s.startswith('0x'):
            s = s[2:]
        # pad if odd length
        if len(s) % 2 != 0:
            s = '0' + s
        bytes_chunks = [s[i:i+2] for i in range(0, len(s), 2)]
        return ''.join(reversed(bytes_chunks))

    return {
        'destination': rev_bytes(payload[0:8]),
        'source': rev_bytes(payload[8:16]),
        'packet_id': rev_bytes(payload[16:24]),
        'flags': rev_bytes(payload[24:26]),
        'channel_hash': rev_bytes(payload[26:28]),
        'next_hop': rev_bytes(payload[28:30]),
        'relay_node': rev_bytes(payload[30:32]),
        'data': payload[32:253]
    }

if __name__ == "__main__":
    import sys
    # if given flag -p, just print process_lora
    if sys.argv[1] == '-p':
        pprint(process_lora(sys.argv[2]))
        sys.exit(0)
    
    if len(sys.argv) != 2:
        print("Usage: python parse_packets.py <file_path>")
        sys.exit(1)
        
    file_path = sys.argv[1]
    pprint(parse_file(file_path))