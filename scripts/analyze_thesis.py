import os
import xml.etree.ElementTree as ET
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np

# Config
RESULTS_DIR = "results"
OUT_DIR = "/home/emre/.gemini/antigravity/brain/cdc828aa-b740-4037-99c2-a79c7cecd99c"

ENVS = ["enterprise", "airport"]
ARCHS = ["centralized", "distributed", "segmented"]
FAILS = ["baseline", "core_failure"]

def parse_time_ns(time_str):
    if not time_str:
        return 0.0
    if time_str.endswith("ns"):
        return float(time_str[:-2])
    return float(time_str)

def parse_xml(xml_file):
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

    lost_packets = total_tx - total_rx
    packet_loss_ratio = (lost_packets / total_tx * 100) if total_tx > 0 else 0
    avg_delay_ms = (total_delay_sum / total_rx / 1e6) if total_rx > 0 else 0
    avg_jitter_ms = (total_jitter_sum / total_rx / 1e6) if total_rx > 0 else 0
    throughput_kbps = (total_rx_bytes * 8) / duration_sum / 1000 if duration_sum > 0 else 0
    
    return {
        "TxPackets": total_tx,
        "RxPackets": total_rx,
        "LostPackets": lost_packets,
        "PacketLoss": round(packet_loss_ratio, 2),
        "Throughput": round(throughput_kbps, 2),
        "Delay": round(avg_delay_ms, 2),
        "Jitter": round(avg_jitter_ms, 2)
    }

def main():
    data = []
    missing = []
    
    for env in ENVS:
        for arch in ARCHS:
            for fail in FAILS:
                fname = f"{env}_{arch}_{fail}_flowmon.xml"
                fpath = os.path.join(RESULTS_DIR, fname)
                
                stats = parse_xml(fpath)
                if stats:
                    row = {"Environment": env, "Architecture": arch, "Scenario": fail, "File": fname}
                    row.update(stats)
                    data.append(row)
                else:
                    missing.append(fname)
                    
    if missing:
        print("MISSING FILES:")
        for m in missing:
            print(f" - {m}")
    else:
        print("All 12 scenarios successfully verified and parsed.")
        
    df = pd.DataFrame(data)
    df.to_csv(os.path.join(OUT_DIR, "results_summary.csv"), index=False)
    print("Saved results_summary.csv")
    
    # Compute FIR
    fir_data = []
    for env in ENVS:
        for arch in ARCHS:
            base_loss = df[(df["Environment"]==env) & (df["Architecture"]==arch) & (df["Scenario"]=="baseline")]["PacketLoss"].values
            fail_loss = df[(df["Environment"]==env) & (df["Architecture"]==arch) & (df["Scenario"]=="core_failure")]["PacketLoss"].values
            
            if len(base_loss) > 0 and len(fail_loss) > 0:
                b_loss = max(base_loss[0], 0.1) # avoid div by zero if base loss is perfectly 0
                f_loss = fail_loss[0]
                fir = max(0, (f_loss - b_loss) / b_loss)
                fir_data.append({"Environment": env, "Architecture": arch, "FIR": round(fir, 2)})

    df_fir = pd.DataFrame(fir_data)
    df_fir.to_csv(os.path.join(OUT_DIR, "fir_results.csv"), index=False)
    print("Saved fir_results.csv")
    
    # Generate publication-ready tables
    df_core = df[df["Scenario"] == "core_failure"]
    
    # Packet Loss Table (Rows: Arch, Cols: Env)
    table_loss = df_core.pivot(index="Architecture", columns="Environment", values="PacketLoss")
    table_loss.to_csv(os.path.join(OUT_DIR, "table_packet_loss.csv"))
    
    table_tput = df_core.pivot(index="Architecture", columns="Environment", values="Throughput")
    table_tput.to_csv(os.path.join(OUT_DIR, "table_throughput.csv"))
    
    table_delay = df_core.pivot(index="Architecture", columns="Environment", values="Delay")
    table_delay.to_csv(os.path.join(OUT_DIR, "table_delay.csv"))
    
    table_fir_pivot = df_fir.pivot(index="Architecture", columns="Environment", values="FIR")
    table_fir_pivot.to_csv(os.path.join(OUT_DIR, "table_fir.csv"))
    
    print("Saved publication-ready tables.")
    
    # Plotting
    def make_bar_chart(table, title, ylabel, fname):
        ax = table.plot(kind="bar", figsize=(8, 6), rot=0)
        plt.title(title, fontsize=14)
        plt.ylabel(ylabel, fontsize=12)
        plt.xlabel("Architecture", fontsize=12)
        plt.grid(axis='y', linestyle='--', alpha=0.7)
        plt.legend(title="Environment")
        plt.tight_layout()
        plt.savefig(os.path.join(OUT_DIR, fname))
        plt.close()

    make_bar_chart(table_loss, "Packet Loss Comparison (Core Failure)", "Packet Loss (%)", "packet_loss_comparison.png")
    make_bar_chart(table_tput, "Throughput Comparison (Core Failure)", "Throughput (kbps)", "throughput_comparison.png")
    make_bar_chart(table_delay, "Delay Comparison (Core Failure)", "Delay (ms)", "delay_comparison.png")
    make_bar_chart(table_fir_pivot, "Failure Impact Ratio (FIR) Comparison", "FIR", "fir_comparison.png")
    print("Saved PNG charts.")
    
    # Sanity Checks
    print("\nSanity Validation:")
    for env in ENVS:
        c_loss = table_loss.loc["centralized", env]
        d_loss = table_loss.loc["distributed", env]
        s_loss = table_loss.loc["segmented", env]
        
        print(f"[{env.upper()}] Centralized Loss: {c_loss}% | Distributed: {d_loss}% | Segmented: {s_loss}%")
        if c_loss > d_loss and c_loss > s_loss:
            print(f"  -> SUCCESS: Centralized has highest packet loss.")
        else:
            print(f"  -> FLAG: Centralized does not have highest packet loss!")
            
        c_tput = table_tput.loc["centralized", env]
        d_tput = table_tput.loc["distributed", env]
        s_tput = table_tput.loc["segmented", env]
        
        if c_tput < d_tput and c_tput < s_tput:
            print(f"  -> SUCCESS: Centralized has lowest throughput.")
        else:
            print(f"  -> FLAG: Centralized does not have lowest throughput!")

if __name__ == "__main__":
    main()
