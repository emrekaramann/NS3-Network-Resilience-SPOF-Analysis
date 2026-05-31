#!/usr/bin/env python3
import xml.etree.ElementTree as ET
import sys
import os
import re

def parse_time_ns(time_str):
    if not time_str:
        return 0.0
    if time_str.endswith("ns"):
        return float(time_str[:-2])
    return float(time_str)

def get_flow_stats(xml_file):
    """
    Parses an NS-3 FlowMonitor XML file and returns an aggregate stats dictionary.
    """
    try:
        if not os.path.exists(xml_file):
            return None
            
        tree = ET.parse(xml_file)
        root = tree.getroot()
        stats = root.find("FlowStats")
        
        if stats is None:
            return None
            
        total_tx = 0
        total_rx = 0
        total_delay_sum = 0.0
        total_jitter_sum = 0.0
        total_rx_bytes = 0
        duration_sum = 0.0

        for flow in stats.findall("Flow"):
            tx_packets = int(flow.get("txPackets", 0))
            rx_packets = int(flow.get("rxPackets", 0))
            rx_bytes = int(flow.get("rxBytes", 0))
            delay_sum = parse_time_ns(flow.get("delaySum"))
            jitter_sum = parse_time_ns(flow.get("jitterSum"))
            
            time_first_rx = parse_time_ns(flow.get("timeFirstRxPacket")) / 1e9
            time_last_rx = parse_time_ns(flow.get("timeLastRxPacket")) / 1e9
            
            duration = time_last_rx - time_first_rx
            
            if duration > 0:
                duration_sum += duration
                total_rx_bytes += rx_bytes
                
            total_tx += tx_packets
            total_rx += rx_packets
            total_delay_sum += delay_sum
            total_jitter_sum += jitter_sum

        packet_loss_ratio = ( (total_tx - total_rx) / total_tx * 100) if total_tx > 0 else 0
        avg_delay_ms = (total_delay_sum / total_rx / 1e6) if total_rx > 0 else 0
        avg_jitter_ms = (total_jitter_sum / total_rx / 1e6) if total_rx > 0 else 0
        throughput_kbps = (total_rx_bytes * 8) / duration_sum / 1000 if duration_sum > 0 else 0
        
        # Extract metadata from filename using regex
        fname = os.path.basename(xml_file)
        
        env = "unknown"
        arch = "unknown"
        fail = "unknown"
        scale = "30" # Default scale
        
        match = re.match(r"([a-z]+)_([a-z]+)_(baseline|core_failure|distribution_failure)(?:_scale(\d+))?_flowmon\.xml", fname)
        if match:
            env = match.group(1)
            arch = match.group(2)
            fail = match.group(3)
            if match.group(4):
                scale = match.group(4)

        return {
            "file": fname,
            "environment": env,
            "architecture": arch,
            "failure": fail,
            "scale": scale,
            "tx_packets": total_tx,
            "rx_packets": total_rx,
            "loss_ratio": round(packet_loss_ratio, 2),
            "throughput_kbps": round(throughput_kbps, 2),
            "delay_ms": round(avg_delay_ms, 2),
            "jitter_ms": round(avg_jitter_ms, 2)
        }
        
    except Exception as e:
        print(f"Failed to parse {xml_file}: {e}")
        return None

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 parse_flowmon.py <flowmon_xml_file> ...")
        sys.exit(1)
        
    for f in sys.argv[1:]:
        res = get_flow_stats(f)
        if res:
            print(f"File: {res['file']}")
            print(f"  Arch: {res['architecture']}, Fail: {res['failure']}")
            print(f"  Throughput: {res['throughput_kbps']} kbps")
            print(f"  Loss: {res['loss_ratio']}%")
            print(f"  Delay: {res['delay_ms']} ms")
            print(f"  Jitter: {res['jitter_ms']} ms\n")
