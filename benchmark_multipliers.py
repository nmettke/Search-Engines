#!/usr/bin/env python3
"""
Benchmark script to find optimal thread multiplier for crawler performance.
Tests different multipliers (1x to 32x cores) by:
1. Modifying Crawler.cpp with the multiplier
2. Rebuilding the project
3. Running for 10 minutes
4. Recording URLs crawled
"""

import os
import re
import subprocess
import time
import signal
import json
from pathlib import Path

# Configuration
CRAWLER_CPP_PATH = "/home/mettke/search_engines/src/crawler/Crawler.cpp"
BUILD_DIR = "/home/mettke/search_engines/build"
CRAWLER_BIN = os.path.join(BUILD_DIR, "bin", "crawler")
RUN_TIME_SECONDS = 600  # 10 minutes per test (20 coarse tests * 10min + ~21 fine tests = ~7 hours)
MIN_MULTIPLIER = 1
MAX_MULTIPLIER = 200

def modify_multiplier(multiplier):
    """Modify Crawler.cpp to use the specified multiplier."""
    with open(CRAWLER_CPP_PATH, 'r') as f:
        content = f.read()
    
    # Replace the multiplier
    new_content = re.sub(
        r'size_t ThreadCount = cores \* \d+;',
        f'size_t ThreadCount = cores * {multiplier};',
        content
    )
    
    if new_content == content:
        print(f"ERROR: Could not find ThreadCount line in {CRAWLER_CPP_PATH}")
        return False
    
    with open(CRAWLER_CPP_PATH, 'w') as f:
        f.write(new_content)
    
    print(f"✓ Modified Crawler.cpp: cores * {multiplier}")
    return True

def rebuild_project():
    """Rebuild the project using CMake."""
    print("Building project...")
    try:
        result = subprocess.run(
            ["make", "-j", str(os.cpu_count() or 4)],
            cwd=BUILD_DIR,
            capture_output=True,
            text=True,
            timeout=300
        )
        if result.returncode != 0:
            print(f"Build failed:\n{result.stderr}")
            return False
        print("✓ Build successful")
        return True
    except subprocess.TimeoutExpired:
        print("Build timed out")
        return False
    except Exception as e:
        print(f"Build error: {e}")
        return False

def run_crawler(multiplier):
    """Run the crawler for RUN_TIME_SECONDS and return URLs crawled."""
    if not os.path.exists(CRAWLER_BIN):
        print(f"ERROR: Crawler binary not found at {CRAWLER_BIN}")
        return None
    
    print(f"\n{'='*60}")
    print(f"Testing multiplier: {multiplier}x cores")
    print(f"{'='*60}")
    print(f"Running for {RUN_TIME_SECONDS} seconds...")
    
    try:
        process = subprocess.Popen(
            [CRAWLER_BIN],
            cwd="/home/mettke/search_engines",
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        
        # Run for specified time
        time.sleep(RUN_TIME_SECONDS)
        
        # Send SIGINT to gracefully stop
        process.send_signal(signal.SIGINT)
        stdout, stderr = process.communicate(timeout=30)
        
        # Extract final crawl count from output
        # Output format: "Crawled [count] url"
        matches = re.findall(r'Crawled \[(\d+)\]', stdout)
        if matches:
            final_count = int(matches[-1])
            print(f"✓ Final count: {final_count} URLs")
            return final_count
        else:
            print(f"WARNING: Could not parse crawl count from output")
            print(f"Last few lines of output:\n{stdout[-500:]}")
            return None
            
    except subprocess.TimeoutExpired:
        process.kill()
        print("ERROR: Crawler did not stop gracefully")
        return None
    except Exception as e:
        print(f"ERROR running crawler: {e}")
        return None

def main():
    """Main benchmark function with coarse-to-fine search."""
    if not os.path.exists(CRAWLER_CPP_PATH):
        print(f"ERROR: {CRAWLER_CPP_PATH} not found")
        return
    
    if not os.path.exists(BUILD_DIR):
        print(f"ERROR: Build directory {BUILD_DIR} not found")
        return
    
    results = {}
    
    try:
        # Phase 1: Coarse search (increments of 10)
        print("PHASE 1: Coarse Search (increments of 10)")
        print(f"{'='*60}")
        coarse_results = {}
        
        for multiplier in range(MIN_MULTIPLIER, MAX_MULTIPLIER + 1, 10):
            # Modify source
            if not modify_multiplier(multiplier):
                print(f"Failed to modify multiplier to {multiplier}")
                continue
            
            # Rebuild
            if not rebuild_project():
                print(f"Failed to rebuild for multiplier {multiplier}")
                continue
            
            # Run benchmark
            urls_crawled = run_crawler(multiplier)
            if urls_crawled is not None:
                coarse_results[multiplier] = urls_crawled
                results[multiplier] = urls_crawled
                print(f"Throughput: {urls_crawled / RUN_TIME_SECONDS:.2f} URLs/sec")
            else:
                print(f"Failed to get results for multiplier {multiplier}")
        
        # Find best range
        if coarse_results:
            sorted_coarse = sorted(coarse_results.items(), key=lambda x: x[1], reverse=True)
            best_multiplier = sorted_coarse[0][0]
            
            # Determine search range (±10 around best, but within bounds)
            search_start = max(MIN_MULTIPLIER, best_multiplier - 10)
            search_end = min(MAX_MULTIPLIER, best_multiplier + 10)
            
            print(f"\nBest coarse result: {best_multiplier}x cores")
            print(f"\nPHASE 2: Fine Search ({search_start}-{search_end})")
            print(f"{'='*60}")
            
            # Phase 2: Fine search (all values in range)
            for multiplier in range(search_start, search_end + 1):
                # Skip if already tested in phase 1
                if multiplier in results:
                    print(f"Skipping {multiplier}x (already tested)")
                    continue
                
                # Modify source
                if not modify_multiplier(multiplier):
                    print(f"Failed to modify multiplier to {multiplier}")
                    continue
                
                # Rebuild
                if not rebuild_project():
                    print(f"Failed to rebuild for multiplier {multiplier}")
                    continue
                
                # Run benchmark
                urls_crawled = run_crawler(multiplier)
                if urls_crawled is not None:
                    results[multiplier] = urls_crawled
                    print(f"Throughput: {urls_crawled / RUN_TIME_SECONDS:.2f} URLs/sec")
                else:
                    print(f"Failed to get results for multiplier {multiplier}")
        
    except KeyboardInterrupt:
        print("\n\nBenchmark interrupted by user")
    
    finally:
        # Print summary
        print(f"\n{'='*60}")
        print("REGRESSION ANALYSIS RESULTS")
        print(f"{'='*60}")
        
        if results:
            sorted_results = sorted(results.items(), key=lambda x: x[1], reverse=True)
            
            # Show top 10 results
            print("\nTop 10 multipliers by throughput:")
            for i, (multiplier, urls) in enumerate(sorted_results[:10], 1):
                throughput = urls / RUN_TIME_SECONDS
                print(f"{i:2d}. {multiplier:3d}x cores: {urls:6d} URLs ({throughput:6.2f} URLs/sec)")
            
            # Show bottom 5
            print("\nBottom 5 multipliers:")
            for i, (multiplier, urls) in enumerate(sorted_results[-5:], 1):
                throughput = urls / RUN_TIME_SECONDS
                print(f"{multiplier:3d}x cores: {urls:6d} URLs ({throughput:6.2f} URLs/sec)")
            
            best_multiplier = sorted_results[0][0]
            best_urls = sorted_results[0][1]
            best_throughput = best_urls / RUN_TIME_SECONDS
            
            # Calculate statistics
            all_urls = [u for u in results.values()]
            avg_urls = sum(all_urls) / len(all_urls)
            
            print(f"\n{'='*60}")
            print(f"Optimal multiplier: {best_multiplier}x cores")
            print(f"Best throughput: {best_throughput:.2f} URLs/sec ({best_urls} URLs in {RUN_TIME_SECONDS}s)")
            print(f"Average throughput: {avg_urls / RUN_TIME_SECONDS:.2f} URLs/sec")
            print(f"Tests completed: {len(results)}/{MAX_MULTIPLIER - MIN_MULTIPLIER + 1}")
            print(f"{'='*60}")
            
            # Save detailed results
            with open("/home/mettke/search_engines/benchmark_results.json", 'w') as f:
                json.dump({
                    "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
                    "run_time_seconds": RUN_TIME_SECONDS,
                    "multiplier_range": f"{MIN_MULTIPLIER}-{MAX_MULTIPLIER}",
                    "results": results,
                    "optimal_multiplier": best_multiplier,
                    "optimal_throughput": best_throughput
                }, f, indent=2)
            print("\nDetailed results saved to benchmark_results.json")
        else:
            print("No results collected")

if __name__ == "__main__":
    main()
