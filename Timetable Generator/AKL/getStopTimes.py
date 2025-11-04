import requests
import json
from pathlib import Path
import time
import os


AT_API_KEY = "Put-Your-API-Key-Here"


def time_to_seconds(time_str):
    """Convert a time string 'HH:MM:SS' to seconds since midnight."""
    h, m, s = map(int, time_str.split(":"))
    return h * 3600 + m * 60 + s


def transform_stop_times(stop_times_data):
    """Transform stop times data to the desired format."""
    if not stop_times_data or "data" not in stop_times_data:
        return {}

    # Get the first stop time to establish the route start time
    first_stop = stop_times_data["data"][0]
    route_start_time = time_to_seconds(first_stop["attributes"]["arrival_time"])

    # Process all stops for this trip
    transformed = []

    for stop in stop_times_data["data"]:
        stop_id = stop["attributes"]["stop_id"]
        arrival_time = time_to_seconds(stop["attributes"]["arrival_time"])

        # Calculate seconds since route start time
        seconds_since_start = arrival_time - route_start_time

        # Add to transformed data
        transformed.append({stop_id: seconds_since_start})

    return transformed


def get_stop_times(route_id):
    url = f"https://api.at.govt.nz/gtfs/v3/trips/{route_id}/stoptimes"
    headers = {"Ocp-Apim-Subscription-Key": AT_API_KEY}

    response = requests.get(url, headers=headers)

    if response.status_code == 200:
        stop_times = response.json()
        return stop_times
    else:
        print(f"Error: {response.status_code}")
        return None


def getTrackedTrains():
    url = "http://170.64.177.77:3000/akl-ltm/api/trackedtrains"

    try:
        response = requests.get(url)
        response.raise_for_status()
        trains = response.json()

        # Store only trains with all required fields
        stored_trains = []
        for train in trains:
            # Check if all required fields exist
            trip_id = train.get("tripId")
            route = train.get("route")
            current_block = train.get("currentBlock")
            previous_block = train.get("previousBlock")
            timestamp = train.get("position", {}).get("timestamp")

            # Only include trains with all required fields
            if all(
                field is not None
                for field in [trip_id, route, current_block, previous_block, timestamp]
            ):
                train_info = {
                    "tripId": trip_id,
                    "route": route,
                    "currentBlock": current_block,
                    "previousBlock": previous_block,
                    "timestamp": timestamp,
                }
                stored_trains.append(train_info)

        return stored_trains

    except requests.exceptions.RequestException as e:
        print(f"Error fetching data: {e}")
        return []
    except Exception as e:
        print(f"Error processing data: {e}")
        return []


def tripRunsOnDay(trip_id, route_id, day_str):
    """
    Check if a trip runs on a specific day.
    """

    global service_calendars, routes

    # Get the trip details
    route_trips = routes.get(route_id, {})

    trip_service_id = None
    for trip in route_trips["data"]:
        if trip["id"] == trip_id:
            trip_service_id = trip["attributes"]["service_id"]
            break

    if trip_service_id and "Other" in trip_service_id:
        print(f"{route_id} Trip {trip_id} has service_id: {trip_service_id}")
        return False

    # Get the service calendar for the route
    service_calendar = service_calendars.get(trip_service_id)
    if not service_calendar:
        print(f"No service calendar found for service_id: {trip_service_id}")
        return False

    # Check if the trip's day is in the service calendar
    return service_calendar["data"]["attributes"].get(day_str, 0) == 1


def group_trips_by_pattern(trips):
    """
    Group trip_ids that have the same stopping pattern (same stops and times).

    Args:
        trips (dict): Dictionary with trip_id as key and list of {stop_id: time} as value

    Returns:
        dict: Grouped trips with pattern signature as key and list of trip_ids as value
    """
    patterns = {}

    for trip_id, trip_data in trips.items():

        stops = trip_data["stops"]
        # Create a signature for this trip's pattern
        # Convert list of dicts to a tuple of (stop_id, time) pairs
        pattern_signature = tuple(
            (list(stop.keys())[0], list(stop.values())[0]) for stop in stops
        )

        # Group trips by their pattern signature
        if pattern_signature not in patterns:
            patterns[pattern_signature] = []

        if tripRunsOnDay(trip_id, trip_data["route"], "wednesday"):
            patterns[pattern_signature].append(trip_id)

    # Convert to a more readable format
    grouped_trips = {}
    for i, (pattern, trip_ids) in enumerate(patterns.items(), 1):
        # Use the route of the first trip in the group as the prefix
        if trip_ids:
            first_route = trips[trip_ids[0]]["route"] if trip_ids else "UNKNOWN"
            pattern_key = f"{first_route}_Pattern_{i}"
            grouped_trips[pattern_key] = {
                "trip_ids": trip_ids,
                "start_times": [trips[trip_id]["start_time"] for trip_id in trip_ids],
                "routes": [trips[trip_id]["route"] for trip_id in trip_ids],
            }

    return grouped_trips


def update_block_schedule(timetable, trips, tracked_trains, block_schedule_file):
    """
    Update block_schedule.json with the format:
      "<pattern_key>": {
        "start_times": [<start_time>, ...],
        "blocks_times": {
          "<block_number>": [<seconds_since_start>, ...],
          ...
        }
      },
      ...
    The block_times are seconds since the start_time of the trip.
    The block number is train["currentBlock"].
    """
    # Load existing block_schedule if it exists
    if os.path.exists(block_schedule_file):
        with open(block_schedule_file, "r") as f:
            try:
                block_schedule = json.load(f)
            except Exception:
                block_schedule = {}
                print("Error loading existing block_schedule.json, starting fresh.")
    else:
        block_schedule = {}

    # Build a mapping from trip_id to pattern_key
    trip_to_pattern = {}
    for pattern_key, pattern_data in timetable.items():
        for trip_id in pattern_data["trip_ids"]:
            trip_to_pattern[trip_id] = pattern_key

    # Initialize block_schedule by pattern_key
    for pattern_key, pattern_data in timetable.items():
        if pattern_key not in block_schedule:
            block_schedule[pattern_key] = {
                "start_times": pattern_data["start_times"],
                "blocks_times": {},
            }
        else:
            block_schedule[pattern_key]["start_times"] = pattern_data["start_times"]

    # For each tracked train, add block times to the correct pattern
    epoch_at_midnight = int(
        time.mktime(time.strptime(time.strftime("%Y-%m-%d"), "%Y-%m-%d"))
    )

    for train in tracked_trains:
        trip_id = train["tripId"]
        block = str(train["currentBlock"])
        if trip_id in trip_to_pattern and trip_id in trips:
            pattern_key = trip_to_pattern[trip_id]

            # Only append block_time if the block has changed
            # Check if previousBlock and currentBlock exists
            if int(train.get("currentBlock")) != int(train.get("previousBlock")):

                timestamp_since_trip_start = (
                    train["timestamp"]
                    - epoch_at_midnight
                    - trips[trip_id]["start_time"]
                )
                # Append the block time to the schedule
                if block not in block_schedule[pattern_key]["blocks_times"]:
                    block_schedule[pattern_key]["blocks_times"][block] = []
                block_schedule[pattern_key]["blocks_times"][block].append(
                    timestamp_since_trip_start
                )

    # Save block_schedule
    with open(block_schedule_file, "w") as f:
        json.dump(block_schedule, f, indent=4)


if __name__ == "__main__":
    at_requests = 0
    trip_file = "Timetable Generator/AKL/trips.json"
    timetable_file = "Timetable Generator/AKL/timetable.json"
    block_schedule_file = "Timetable Generator/AKL/block_schedule.json"
    service_calendars_file = "Timetable Generator/AKL/service-calendars.json"
    routes_dir = "Timetable Generator/AKL/routes"

    # Load service calendars from json if it exists
    if Path(service_calendars_file).exists():
        with open(service_calendars_file, "r") as f:
            service_calendars = json.load(f)
    else:
        print("No service calendars file found")
        service_calendars = {}

    # Load routes from json files in routes directory
    routes = {}
    for route_file in Path(routes_dir).glob("*-trips.json"):
        with open(route_file, "r") as f:
            route_data = json.load(f)
            route_id = route_file.stem.replace("-trips", "")
            routes[route_id] = route_data

    # Get trips from json if it exists
    if Path(trip_file).exists():
        with open("Timetable Generator/AKL/trips.json", "r") as f:
            trips = json.load(f)
    else:
        print("No trips file found")
        trips = {}

    while True:
        tracked_trains = getTrackedTrains()

        # Create trips.json by getting stop times for each tripId in tracked trains
        # for train in tracked_trains:
        #     trip_id = train["tripId"]
        #     route = train["route"]
        #     if trip_id not in trips:
        #         at_requests += 1
        #         stop_times = get_stop_times(trip_id)
        #         if stop_times:
        #             transformed_stops = transform_stop_times(stop_times)
        #             if transformed_stops:
        #                 first_stop = stop_times["data"][0]
        #                 start_time = time_to_seconds(
        #                     first_stop["attributes"]["arrival_time"]
        #                 )
        #                 trips[trip_id] = {
        #                     "route": route,
        #                     "start_time": start_time,
        #                     "stops": transformed_stops,
        #                 }
        #                 print(
        #                     f"Added trip {trip_id} with {len(transformed_stops)} stops"
        #                 )

        #                 # Save trips to json
        #                 with open(trip_file, "w") as f:
        #                     json.dump(trips, f, indent=4)

        timetable = group_trips_by_pattern(trips)
        # Save timetable to json
        with open(timetable_file, "w") as f:
            json.dump(timetable, f, indent=4)

        # Update block_schedule.json for use in LED-Rails-Firmware
        update_block_schedule(timetable, trips, tracked_trains, block_schedule_file)

        print(f"Total AT API requests made: {at_requests}\n")

        # Wait for 19 seconds
        time.sleep(19)
