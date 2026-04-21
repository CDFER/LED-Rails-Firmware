import json
import os
import pandas as pd

def load_mapped_stops():
    # Get the directory where this script is located
    script_dir = os.path.dirname(os.path.abspath(__file__))
    file_path = os.path.join(script_dir, 'mapped_stops.json')
    
    with open(file_path, 'r') as f:
        data = json.load(f)
        
    # Create a dictionary for fast lookup: {stop_id: block}
    mapped_stops = {}
    for item in data:
        if 'stop_id' in item and 'blocks' in item:
            mapped_stops[item['stop_id']] = item['blocks']
        elif 'stop_id' in item and 'block' in item:
                mapped_stops[item['stop_id']] = item['block']

    return mapped_stops

def get_stop_ids_for_trip(trip_id):
    script_dir = os.path.dirname(os.path.abspath(__file__))
    stop_times_path_txt = os.path.join(script_dir, 'stop_times.txt')
    
    stop_times_path = None
    if os.path.exists(stop_times_path_txt):
        stop_times_path = stop_times_path_txt
    else:
        print("Error: stop_times.txt not found.")
        return []

    # Read the stop_times file
    # Ensure stop_id and trip_id are read as strings to preserve leading zeros if any
    try:
        df = pd.read_csv(stop_times_path, dtype={'stop_id': str, 'trip_id': str, 'stop_sequence': int})
    except Exception as e:
        print(f"Error reading stop_times file: {e}")
        return []
        
    # Filter for the trip_id
    trip_stops = df[df['trip_id'] == trip_id].copy()

    if trip_stops.empty:
        print(f"No stops found for trip_id: {trip_id}")
        return []
    
    # Sort by stop_sequence
    trip_stops.sort_values('stop_sequence', inplace=True)
    
    return trip_stops['stop_id'].tolist()

if __name__ == "__main__":
    # Load the map of stops to blocks
    stops_map = load_mapped_stops()
    print(f"Loaded {len(stops_map)} mapped stops.")
    
    # 1. Get the list of stops for a specific trip
    trip_id = "02-ALM--1-T2-2302" # Example trip from trips.txt
    print(f"\nProcessing trip: {trip_id}")
    
    trip_stops = get_stop_ids_for_trip(trip_id)
    print(f"Found {len(trip_stops)} stops for trip {trip_id}")

    # 2. Map these stops to block IDs
    block_sequence = []
    for stop_id in trip_stops:
        if stop_id in stops_map:
            # Add block(s) to sequence
            val = stops_map[stop_id]
            if isinstance(val, list):
                block_sequence.extend(val)
            else:
                block_sequence.append(val)
        else:
            print(f"Warning: Stop {stop_id} not found in mapped stops.")
            
    print(f"Block sequence: {block_sequence}")

