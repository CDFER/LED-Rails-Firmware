import json
import os
import pandas as pd


def load_track_blocks():
    # Get the directory where this script is located
    script_dir = os.path.dirname(os.path.abspath(__file__))
    file_path = os.path.join(script_dir, 'trackBlocks.json')

    try:
        with open(file_path, 'r') as f:
            data = json.load(f)
            
        # Convert list of lists to a dictionary for easier lookup if needed
        # Structure seems to be [[stop_id, [blocks...]], ...]
        track_blocks = {item[0]: item[1] for item in data if len(item) >= 2}
        return track_blocks
        
    except FileNotFoundError:
        print(f"Error: Could not find {file_path}")
        return {}
    except json.JSONDecodeError:
        print(f"Error: Could not parse {file_path}")
        return {}

def is_point_in_polygon(lat, lon, polygon):
    """
    Ray-casting algorithm to determine if a point (lat, lon) is inside a polygon.
    The polygon is a list of [lat, lon] points.
    """
    x, y = lat, lon
    n = len(polygon)
    inside = False
    p1x, p1y = polygon[0]
    for i in range(n + 1):
        p2x, p2y = polygon[i % n]
        if y > min(p1y, p2y):
            if y <= max(p1y, p2y):
                if x <= max(p1x, p2x):
                    if p1y != p2y:
                        xinters = (y - p1y) * (p2x - p1x) / (p2y - p1y) + p1x
                    if p1x == p2x or x <= xinters:
                        inside = not inside
        p1x, p1y = p2x, p2y
    return inside

def map_stops_to_blocks():
    # Load track blocks
    track_blocks = load_track_blocks()
    
    # Load stops.txt
    script_dir = os.path.dirname(os.path.abspath(__file__))
    stops_file = os.path.join(script_dir, 'stops.txt')
    
    if not os.path.exists(stops_file):
        print(f"Error: Could not find {stops_file}")
        return

    # Use pandas to read the CSV file
    try:
        stops_df = pd.read_csv(stops_file)
        print(f"Loaded {len(stops_df)} stops.")
    except Exception as e:
        print(f"Error reading stops.txt: {e}")
        return

    # Prepare polygons
    # track_blocks is  {block_id: { "polygon": [[lat, lon], ...], ... }}
    # We want to iterate over all blocks and check if a stop is inside.
    
    results = []

    for index, row in stops_df.iterrows():
        try:
            lat = float(row['stop_lat'])
            lon = float(row['stop_lon'])
            stop_id = row['stop_id']
            # Safely get stop name if it exists, otherwise use stop_id
            stop_name = row.get('stop_name', str(stop_id))
            
            if row.get('platform_code', None):
                stop_name += " Platform:" + str(row['platform_code'])
            
            for block_id, data in track_blocks.items():
                if 'polygon' not in data:
                    continue
                
                polygon = data['polygon']
                
                # Manual check
                blockNumber = None
                if is_point_in_polygon(lat, lon, polygon):
                    if data.get('platforms'):
                        for platform in data.get('platforms', []):
                            if stop_id in platform.get('stop_ids', []):
                                blockNumber = platform.get('blockNumber')                             
                    else:
                        # If no platforms, just add the block
                        blockNumber = data.get('blockNumber')
                        
                        
                    result_entry = {
                        "stop_id": stop_id,
                        "stop_name": stop_name,
                        "block": blockNumber
                    }
                    results.append(result_entry)
                    print(f"Stop {stop_name} ({stop_id}) is in block: {blockNumber}")
                
        except ValueError:
            continue

    print(f"Found {len(results)} stops inside known blocks.")
    return results

if __name__ == "__main__":
    mapped_stops = map_stops_to_blocks()
    
    # Save the result to a JSON file
    output_file = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'mapped_stops.json')
    with open(output_file, 'w') as f:
        json.dump(mapped_stops, f, indent=2)
    print(f"Saved mapped stops to {output_file}")
