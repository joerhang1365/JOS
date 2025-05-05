import math
import sys

def calculate_entropy(byte_counts):
    total_count = sum(byte_counts)

    # Calculate the probability distribution
    probabilities = [count / total_count for count in byte_counts if count > 0]

    # Compute Shannon entropy
    entropy = -sum(p * math.log2(p) for p in probabilities)
    return entropy

def main():
    byte_counts = []

    # Read byte counts from stdin (one integer per line)
    for line in sys.stdin:
        try:
            byte_counts.append(int(line.strip()))
        except ValueError:
            continue  # Skip invalid lines, if any

    # Ensure that exactly 256 counts were provided
    print("# bytes: ", len(byte_counts))
    if len(byte_counts) != 256:
        raise ValueError("Exactly 256 byte counts must be provided.")

    # Calculate and print the Shannon entropy
    entropy = calculate_entropy(byte_counts)

    if (entropy > 7.99): # sharvil wanted more nines
        print("PASS")
    else:
        print("FAIL")

if __name__ == "__main__":
    main()
