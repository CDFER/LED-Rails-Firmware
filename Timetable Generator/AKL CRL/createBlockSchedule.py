import json

interval_seconds = 60 * 15  # 15 minutes

# East West Line (Swanson to Manukau)

swanson_to_mt_eden = [
    (0, 100),
    (57, 101),
    (90, 102),
    (128, 103),
    (193, 104),
    (270, 105),
    (371, 106),
    (441, 107),
    (522, 108),
    (597, 110),
    (691, 112),
    (765, 113),
    (823, 114),
    (884, 115),
    (966, 116),
    (1058, 117),
    (1153, 118),
    (1262, 119),
    (1361, 120),
    (1428, 121),
    (1496, 122),
    (1569, 123),
    (1686, 124),
    (1771, 125),
    (1829, 126),
    (1928, 127),
    (1963, 128),
    (2014, 129),
    (2101, 131),
    (2152, 132),
    (2230, 133),
]

# ( Should be approx 700 seconds)
crl = [
    (0, 133),
    (100, 308),  # Maungawhau
    (200, 309),
    (300, 310),  # Karanga-a-Hape
    (400, 311),
    (500, 312),  # Te Waihorotiu
    (600, 313),
    (650, 314),
    (700, 315),  # Britomart
]

# Britomart to Manukau
britomart_to_manukau = [
    (0, 315),
    (57, 317),
    (115, 318),
    (139, 319),
    (164, 321),
    (200, 322),
    (241, 324),
    (292, 325),
    (333, 326),
    (379, 327),
    (440, 328),
    (519, 329),
    (594, 330),
    (668, 331),
    (724, 332),
    (764, 333),
    (844, 334),
    (896, 335),
    (964, 336),
    (1042, 337),
    (1109, 338),
    (1156, 339),
    (1227, 340),
    (1310, 341),
    (1354, 159),
    (1423, 160),
    (1479, 162),
    (1594, 163),
    (1653, 164),
    (1671, 165),
    (1747, 166),
    (1865, 167),
    (1933, 168),
    (2015, 170),
    (2094, 171),
    (2157, 342),
    (2185, 343),
    (2279, 344),
]


# Calculate the end time of the first segment
first_segment_end_time = swanson_to_mt_eden[-1][0]

# Adjust CRL timestamps to start after the first segment
crl_adjusted = [(time + first_segment_end_time, block) for time, block in crl]

# Calculate the end time of the second segment (CRL)
second_segment_end_time = crl_adjusted[-1][0]

# Adjust Britomart to Manukau timestamps to start after the CRL
britomart_to_manukau_adjusted = [
    (time + second_segment_end_time, block) for time, block in britomart_to_manukau
]

# Combine all segments into one continuous schedule
block_schedule = swanson_to_mt_eden + crl_adjusted + britomart_to_manukau_adjusted

total_duration = block_schedule[-1][0]

# Create the return trip by flipping timestamps and reversing the order
return_block_schedule = []
for time, block in reversed(block_schedule):
    flipped_time = total_duration + 191 + (total_duration - time)
    return_block_schedule.append((flipped_time, block))

# Set the last timestamp to exactly 10800 seconds (3 hours)
# block_schedule.append((10800, -1))

# Print the final schedule and verify the last timestamp
print("Block Schedule:")
for time, block in block_schedule:
    print(f"{{ {time}, {block} }}")


# Create blocks_times dictionary
blocks_times = {}
return_block_times = {}

start_times = list(range(0, 24 * 60 * 60, interval_seconds))

# For each start time, map blocks to their relative times
for i, (time, block) in enumerate(block_schedule):
    if block not in blocks_times:
        blocks_times[block] = [time]

# For each start time, map blocks to their relative times
for i, (time, block) in enumerate(return_block_schedule):
    if block not in return_block_times:
        return_block_times[block] = [time]

# Structure the data
output_data = {
    "EAST-WEST-0": {"start_times": start_times, "blocks_times": blocks_times},
    "EAST-WEST-1": {"start_times": start_times, "blocks_times": return_block_times},
}

# Write to JSON file
with open("Timetable Generator/AKL CRL/block_schedule.json", "w") as f:
    json.dump(output_data, f, indent=4)

print("block_schedule.json has been created.")
