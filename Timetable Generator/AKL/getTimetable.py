import os
import requests
import json

AT_API_KEY = "3d74a150df454381aefb00ce905a6434"

# https://api.at.govt.nz/gtfs/v3/services/{service_id}/calendars

api_request_count = 0


def get_route_trips(route_id):
    url = f"https://api.at.govt.nz/gtfs/v3/routes/{route_id}/trips"
    headers = {"Ocp-Apim-Subscription-Key": AT_API_KEY}

    response = requests.get(url, headers=headers)
    global api_request_count
    api_request_count += 1

    if response.status_code == 200:
        calendar_data = response.json()
        return calendar_data
    else:
        print(f"Error: {response.status_code}")
        return None


def get_service_calendar(service_id):
    url = f"https://api.at.govt.nz/gtfs/v3/services/{service_id}/calendars"
    headers = {"Ocp-Apim-Subscription-Key": AT_API_KEY}

    response = requests.get(url, headers=headers)
    global api_request_count
    api_request_count += 1

    if response.status_code == 200:
        calendar_data = response.json()
        return calendar_data
    else:
        print(f"Error: {response.status_code}")
        return None


route = "EAST-201"
route_trips = get_route_trips(route)

# Save the fetched route trips data to route-trips.json
if route_trips:
    with open(f"Timetable Generator/AKL/routes/{route}-trips.json", "w") as f:
        json.dump(route_trips, f, indent=4)

# Read service-calendars.json
if os.path.exists("Timetable Generator/AKL/service-calendars.json"):
    with open("Timetable Generator/AKL/service-calendars.json", "r") as f:
        service_calendars = json.load(f)
else:
    service_calendars = {}

# service = "Other-55"
for trip in route_trips["data"]:
    service = trip["attributes"]["service_id"]
    if service in service_calendars:
        # print(
        #     f"Service {service} already exists in service-calendars.json, skipping fetch."
        # )
        continue
    print(f"Fetching calendar for service {service}...")
    calendar = get_service_calendar(service)

    # Save the fetched calendar data to service-calendars.json
    if calendar:
        service_calendars[service] = calendar
        with open("Timetable Generator/AKL/service-calendars.json", "w") as f:
            json.dump(service_calendars, f, indent=4)


# Print the total number of API requests made
print(f"Total API requests made: {api_request_count}")
