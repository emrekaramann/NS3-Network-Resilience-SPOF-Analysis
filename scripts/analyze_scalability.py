import os
import xml.etree.ElementTree as ET
import pandas as pd

RESULTS_DIR = "results"
OUT_DIR = "/home/emre/.gemini/antigravity/brain/cdc828aa-b740-4037-99c2-a79c7cecd99c"

ARCHS = ["centralized", "distributed", "segmented"]
SCALES = [30, 100, 200]
FAILS = ["baseline", "core_failure"]

def parse_time_ns(time_str):
    if not time_str: return 0.0
    if time_str.endswith("ns"): return float(time_str[:-2])
    return float(time_str)

def parse_xml(xml_file):
    if not os.path.exists(xml_file): return None
    tree = ET.parse(xml_file)
    stats = tree.getroot().find("FlowStats")
    if stats is None: return None
        
    total_tx = 0
    total_rx = 0
    total_delay_sum = 0.0
    total_rx_bytes = 0
    duration_sum = 0.0

    for flow in stats.findall("Flow"):
        tx = int(flow.get("txPackets", 0))
        rx = int(flow.get("rxPackets", 0))
        rx_b = int(flow.get("rxBytes", 0))
        d_sum = parse_time_ns(flow.get("delaySum"))
        
        t_first = parse_time_ns(flow.get("timeFirstRxPacket")) / 1e9
        t_last = parse_time_ns(flow.get("timeLastRxPacket")) / 1e9
        dur = t_last - t_first
        
        if dur > 0:
            duration_sum += dur
            total_rx_bytes += rx_b
            
        total_tx += tx
        total_rx += rx
        total_delay_sum += d_sum

    loss_pct = ((total_tx - total_rx) / total_tx * 100) if total_tx > 0 else 0
    delay_ms = (total_delay_sum / total_rx / 1e6) if total_rx > 0 else 0
    tput_kbps = (total_rx_bytes * 8) / duration_sum / 1000 if duration_sum > 0 else 0
    
    return {
        "PacketLoss": round(loss_pct, 2),
        "Throughput": round(tput_kbps, 2),
        "Delay": round(delay_ms, 2)
    }

def main():
    data = []
    
    for scale in SCALES:
        for arch in ARCHS:
            b_stats = parse_xml(os.path.join(RESULTS_DIR, f"airport_{arch}_baseline_scale{scale}_flowmon.xml"))
            f_stats = parse_xml(os.path.join(RESULTS_DIR, f"airport_{arch}_core_failure_scale{scale}_flowmon.xml"))
            
            if b_stats and f_stats:
                b_loss = max(b_stats["PacketLoss"], 0.1)
                f_loss = f_stats["PacketLoss"]
                fir = max(0, (f_loss - b_loss) / b_loss)
                
                data.append({
                    "Scale": scale,
                    "Architecture": arch.capitalize(),
                    "PacketLoss (%)": f_stats["PacketLoss"],
                    "Throughput (kbps)": f_stats["Throughput"],
                    "Delay (ms)": f_stats["Delay"],
                    "FIR": round(fir, 2)
                })

    df = pd.DataFrame(data)
    df.to_csv(os.path.join(OUT_DIR, "scalability_results.csv"), index=False)
    
    # Create pivot table for markdown display
    pivot = df.pivot(index="Scale", columns="Architecture", values=["PacketLoss (%)", "Throughput (kbps)", "FIR"])
    pivot.to_csv(os.path.join(OUT_DIR, "table_scalability.csv"))
    
    print("Scalability extraction complete.")
    print("\nScalability Table:")
    print(df.to_markdown(index=False))

if __name__ == "__main__":
    main()
