import pandas as pd
import requests
import time
import json
from typing import List, Tuple, Dict, Any, Optional
import os


def fetch_tracked_trains() -> Optional[List[Dict[str, Any]]]:
    """
    Fetches tracked train data from the API endpoint.

    Returns:
        List of tracked train dictionaries, or None if request fails.
    """
    try:
        response = requests.get("http://localhost:3000/wlg-ltm/api/trackedtrains")
        response.raise_for_status()  # Raises an HTTPError for bad responses
        return response.json()
    except requests.exceptions.RequestException as e:
        print(f"Error fetching tracked trains: {e}")
        return None
    except ValueError as e:  # JSON decode error
        print(f"Error parsing JSON response: {e}")
        return None


def safe_parse_time(time_str: str) -> pd.Timestamp:
    """
    Safely parse a time string, handling '24:MM:SS' by rolling over to the next day.
    """
    parts = time_str.split(":")
    if len(parts) == 3 and int(parts[0]) >= 24:
        # Roll over to next day, keep minutes and seconds
        hours = int(parts[0])
        minutes = int(parts[1])
        seconds = int(parts[2])
        # Calculate total seconds since midnight
        total_seconds = hours * 3600 + minutes * 60 + seconds
        # Get time modulo 24 hours
        total_seconds = total_seconds % (24 * 3600)
        new_hours = total_seconds // 3600
        new_minutes = (total_seconds % 3600) // 60
        new_seconds = total_seconds % 60
        time_str = f"{new_hours:02}:{new_minutes:02}:{new_seconds:02}"
    return pd.to_datetime(time_str, format="%H:%M:%S")


def save_block_schedules_to_json(
    filename: str = "Timetable Generator/block_schedules.json",
) -> None:
    """
    Saves the current block schedules to a JSON file.

    Args:
        filename: Name of the file to save to (default: "Timetable Generator/block_schedules.json")
    """
    try:
        # Build the requested format
        serializable_schedules = {}
        # Build a reverse lookup: (route, schedule) -> list of trip_ids
        route_schedule_to_trips = {}
        for trip_id, (route, schedule) in trip_to_route_schedule.items():
            key = (route, schedule)
            route_schedule_to_trips.setdefault(key, []).append(trip_id)

        for (route, schedule), blocks in block_schedules.items():
            key = f"{route}_Schedule_{schedule}"
            # Get start times for all trips in this route/schedule
            trip_ids = route_schedule_to_trips.get((route, schedule), [])
            start_times = [
                trip_start_times[tid] for tid in trip_ids if tid in trip_start_times
            ]
            # Convert blocks dict to serializable format
            serializable_blocks = {}
            for block, seconds_list in blocks.items():
                serializable_blocks[str(block)] = seconds_list
            serializable_schedules[key] = {
                "start_times": start_times,
                "blocks_times": serializable_blocks,
            }

        with open(filename, "w") as f:
            json.dump(serializable_schedules, f, indent=2)
        print(f"Block schedules saved to {filename}")
    except Exception as e:
        print(f"Error saving block schedules to {filename}: {e}")


stop_times: pd.DataFrame = pd.read_csv("Timetable Generator/stop_times.csv")

# Global dictionary to store trip_id -> (route, schedule) mapping
trip_to_route_schedule: Dict[str, Tuple[str, int]] = {}

# Global dictionary to store trip start times (trip_id -> start_timestamp)
trip_start_times: Dict[str, int] = {}

# Global dictionary to store block schedules for each (route, schedule)
block_schedules: Dict[Tuple[str, int], Dict[int, List[int]]] = {}


def load_block_schedules_from_json(
    filename: str = "Timetable Generator/block_schedules.json",
) -> None:
    """
    Loads block schedules from a JSON file into the global block_schedules dict.
    """
    global block_schedules, trip_start_times
    if os.path.exists(filename):
        try:
            with open(filename, "r") as f:
                data = json.load(f)
            loaded_block_schedules = {}
            loaded_trip_start_times = {}
            # New format: each key is "route_Schedule_schedule" with 'start_times' and 'blocks_times'
            for key, entry in data.items():
                if "_Schedule_" in key and isinstance(entry, dict):
                    route, sched = key.split("_Schedule_")
                    schedule = int(sched)
                    # Load block times
                    blocks_times = entry.get("blocks_times", {})
                    block_dict = {
                        int(block): times for block, times in blocks_times.items()
                    }
                    loaded_block_schedules[(route, schedule)] = block_dict
                    # Load start times
                    start_times = entry.get("start_times", [])
                    # We don't know trip_ids, but can store start times for reference
                    loaded_trip_start_times[key] = start_times
            block_schedules = loaded_block_schedules
            # Optionally, you can flatten loaded_trip_start_times into trip_start_times if you have trip_ids
            print(f"Loaded block schedules and start times from {filename}")
        except Exception as e:
            print(f"Error loading block schedules from {filename}: {e}")
    else:
        print(f"No existing block schedules file found: {filename}")


# Set to track which trains we've seen to calculate entry times
seen_trains: Dict[str, Tuple[int, str]] = {}  # train_id -> (block, trip_id)


def get_route_schedule_from_trip_id(trip_id: str) -> Optional[Tuple[str, int]]:
    """
    Returns the (route, schedule) tuple for a given trip_id.
    Returns None if the trip_id is not found.
    """
    return trip_to_route_schedule.get(trip_id)


def get_trip_start_time(trip_id: str) -> Optional[int]:
    """
    Returns the start timestamp for a given trip_id.
    Returns None if the trip_id is not found.
    """
    return trip_start_times.get(trip_id)


def update_block_schedule(
    route: str, schedule: int, block: int, seconds_since_start: int
) -> None:
    """
    Updates the block schedule with seconds since start time for a block.
    """
    key = (route, schedule)
    if key not in block_schedules:
        block_schedules[key] = {}

    if block not in block_schedules[key]:
        block_schedules[key][block] = []

    block_schedules[key][block].append(seconds_since_start)


def print_block_schedules() -> None:
    """
    Prints the block schedules for all routes and schedules.
    """
    for (route, schedule), blocks in block_schedules.items():
        print(f"\nRoute: {route} Schedule {schedule}")
        print("Block Schedule:")

        # Collect all timestamps and sort by the earliest occurrence of each block
        block_times = []
        for block, seconds_list in blocks.items():
            if seconds_list:  # Only include blocks with timestamps
                earliest_time = min(seconds_list)
                block_times.append((earliest_time, block))

        # Sort by earliest time
        block_times.sort()

        if block_times:
            for seconds_since_start, block in block_times:
                print(f" - {seconds_since_start} secs @ Block {block}")
        else:
            print(" - No block data collected yet")


def determine_schedule(route: str) -> None:
    # print(f"\nRoute: {route}")

    # Filtering conditions for trips on the specified route
    mask: pd.Series = (
        stop_times["trip_id"].str.contains(route)
        & stop_times["trip_id"].str.contains("MTuWThF")
        & stop_times["trip_id"].str.contains("20250817")
        & (stop_times["stop_sequence"] == 0)
    )

    filtered: pd.DataFrame = stop_times[mask].sort_values("departure_time")
    trips: List[str] = filtered["trip_id"].unique().tolist()

    # Store unique schedules and their numbering
    unique_schedules: List[List[Tuple[int, str]]] = []
    trip_to_schedule: Dict[str, int] = {}

    # Process each trip to extract schedule and store start times
    for trip in trips:
        trip_stops: pd.DataFrame = stop_times[
            stop_times["trip_id"] == trip
        ].sort_values("stop_sequence")

        # Get start time in minutes since midnight
        start_time_str: str = trip_stops.iloc[0]["departure_time"]
        dt_start: pd.Timestamp = safe_parse_time(start_time_str)
        start_minutes: int = dt_start.hour * 60 + dt_start.minute

        # Convert to seconds since midnight
        start_seconds: int = start_minutes * 60 + dt_start.second

        # Store the start time for this trip
        trip_start_times[trip] = start_seconds

        # Normalize stop times relative to start time, handling midnight crossing
        stop_times_list: List[Tuple[int, str]] = []
        for _, row in trip_stops.iterrows():
            dt_stop = safe_parse_time(row["departure_time"])
            stop_minutes = dt_stop.hour * 60 + dt_stop.minute
            offset = stop_minutes - start_minutes
            # If offset is negative, assume trip crossed midnight, add 1440 mins
            if offset < 0:
                offset += 1440
            stop_times_list.append((offset, str(row["stop_id"])))

        # Check if this schedule already exists
        schedule_index: int = -1
        for i, schedule in enumerate(unique_schedules):
            if schedule == stop_times_list:
                schedule_index = i
                break

        # If not found, add it
        if schedule_index == -1:
            unique_schedules.append(stop_times_list)
            schedule_index = len(unique_schedules) - 1

        trip_to_schedule[trip] = schedule_index

    # Store the mapping from trip_id to (route, schedule)
    for trip_id, schedule_index in trip_to_schedule.items():
        trip_to_route_schedule[trip_id] = (route, schedule_index)

    # Print each schedule separately with its start times
    for idx, schedule in enumerate(unique_schedules):
        print(f"\nRoute: {route} Schedule {idx}")
        print("Schedule:")
        for offset, stop_id in schedule:
            print(f" - {offset} mins @ {stop_id}")

        # Print start times for this schedule
        print("\nStart times:")
        schedule_trips = [
            trip for trip, sched_idx in trip_to_schedule.items() if sched_idx == idx
        ]
        for _, row in filtered[filtered["trip_id"].isin(schedule_trips)].iterrows():
            departure_time: str = row["departure_time"]
            print(f" - {departure_time} : {row['trip_id']}")


def monitor_trains(save_interval: int = 60) -> None:
    """
    Continuously monitors trains and updates block schedules.

    Args:
        save_interval: How often to save to JSON file in seconds (default: 60 seconds)
    """
    print("\n=== Starting continuous monitoring ===")
    last_save_time = time.time()

    try:
        while True:
            trains = fetch_tracked_trains()
            if trains:
                for train in trains:
                    if train.get("tripId") and train.get("currentBlock"):
                        train_id = train["trainId"]
                        trip_id = train["tripId"]
                        block = train["currentBlock"]
                        timestamp = train["position"]["timestamp"]

                        result = get_route_schedule_from_trip_id(trip_id)
                        if result:
                            route, schedule = result

                            # Get the trip start time
                            trip_start_time = get_trip_start_time(trip_id)
                            if trip_start_time is not None:
                                # Calculate seconds since start of trip
                                # Assuming timestamp is in seconds since epoch
                                # and we need to convert to seconds since midnight
                                from datetime import datetime

                                dt = datetime.fromtimestamp(timestamp)
                                current_seconds = (
                                    dt.hour * 3600 + dt.minute * 60 + dt.second
                                )

                                # Allow negative seconds_since_start if before trip start
                                seconds_since_start = current_seconds - trip_start_time

                                # Check if this is a new block for this train or first time seeing it
                                prev_block_info = seen_trains.get(train_id)

                                if (
                                    prev_block_info is None
                                    or prev_block_info[0] != block
                                    or prev_block_info[1] != trip_id
                                ):
                                    # Update the block schedule with seconds since start
                                    update_block_schedule(
                                        route, schedule, block, seconds_since_start
                                    )
                                    # Update seen trains
                                    seen_trains[train_id] = (block, trip_id)

                                    print(
                                        f"Train {train_id} entered Block {block} at {seconds_since_start} secs "
                                        f"for Route: {route} Schedule: {schedule}"
                                    )

                # Print updated block schedules
                # print_block_schedules()

                # Periodically save to JSON file
                current_time = time.time()
                if current_time - last_save_time >= save_interval:
                    save_block_schedules_to_json()
                    last_save_time = current_time

            else:
                print("Failed to fetch train data")

            # Wait before next fetch (adjust as needed)
            time.sleep(5)

    except KeyboardInterrupt:
        print("\nMonitoring stopped by user")
        # Save one final time before exiting
        save_block_schedules_to_json()
        print_block_schedules()


if __name__ == "__main__":
    # Load block schedules from file if present
    load_block_schedules_from_json()

    # First, determine all schedules
    determine_schedule("JVL__0")
    determine_schedule("JVL__1")
    determine_schedule("MEL__0")
    determine_schedule("MEL__1")
    determine_schedule("WRL__0")
    determine_schedule("WRL__1")
    determine_schedule("HVL__0")
    determine_schedule("HVL__1")
    determine_schedule("KPL__0")
    determine_schedule("KPL__1")

    # Start continuous monitoring (saves every 120 seconds)
    monitor_trains(save_interval=120)
