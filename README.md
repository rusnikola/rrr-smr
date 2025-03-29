# RRR-SMR

Please see the detailed installation and usage instructions in the artifact.

## Building

```
cd RRR-SMR
make
```

## Usage Example

Test linked list (the DW prefix stands for the new RRR-safe version proposed
in the paper). In the example below, 60 is 60 seconds, P is the pairwise experiment, 128B is the size of the payload, 4096 is the key range, and 50\% is the recycling percentage. 


```
./bench list 60 P 128B 4096 50%
```

For tree, the DW prefix means the same.

For queue, we implemented our new (ModQueue) queue which provides ABA and RRR safety properties but is more memory efficient. We also implemented classical MS ABA-safe and non-ABA-safe queues.

## Running tests

If you go to Scripts, you can run either a lightweight test:

```
nohup ./source_lightweight.sh &
```

or a full-blown test (runs around 26 hours):

```
nohup ./source.sh &
```

## Data

The output and charts of our runs on our machine is stored in Artifact\_Data. When running experiment scripts, they will create a similar Data directory. You can then generate charts by running:

```
python3 generate_charts.py

```

## License

See LICENSE and individual file statements for more information.
