#!/bin/bash

# Set the paths to the directories containing the libraries
arm_path="./arm/bin"
x64_path="./x64/bin"
universal_path="./universal/bin"

# Create the universal library directory if it doesn't exist
mkdir -p "${universal_path}"

# Loop through all the .a files in the arm and x64 directories
for file in "${arm_path}"/* "${x64_path}"/*; do
    # Get the base file name without the directory path or extension
    base_name=$(basename "${file}" )

    # Use lipo to create a universal binary that contains both arm and x64 architectures
    lipo -create "${arm_path}/${base_name}" "${x64_path}/${base_name}" -output "${universal_path}/${base_name}"
done
