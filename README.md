# RB-tree Block Space Management (RBSM)

A high-performance dynamic storage allocation system that combines red-black trees with block groups for efficient space management. This implementation achieves up to 59x higher operation throughput compared to optimized bitmap-based allocation while maintaining significantly lower fragmentation.

## Features

- Efficient best-fit allocation through red-black tree traversal
- Block group organization for improved locality
- Automatic space coalescing to minimize fragmentation
- Low memory overhead that scales with free space regions
- Sub-microsecond allocation and deallocation times
- Excellent fragmentation resistance (0.438% vs 66-68% in bitmap approaches)

## Performance

- **Throughput**: 16.2M operations/second (vs 274.3K ops/sec for optimized bitmap)
- **Fragmentation**: 0.438% average (vs 66-68% for bitmap approaches)
- **Memory Efficiency**: ~5.5KB overhead for managing 1GB space
- **Scalability**: Performance scales with number of free regions rather than total size

## Building and Running

```bash
gcc -O2 space_bench.c -o space_bench
./space_bench
```

The benchmark will run three implementations:
1. RBSM (RB-tree Block Space Management)
2. Simple bitmap allocator
3. Optimized bitmap with block groups

For detailed technical information, please refer to the paper: [RB-tree Block Space Management](https://gist.github.com/mehrantsi/f704131cd6de321041a5c4b4c6e02e8a)

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Author

Mehran Toosi
