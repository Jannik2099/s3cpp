## list_all_objects

This tool lists all objects in the specified bucket.

### why and when to use this over a `ListObjectsV2` loop

I have discovered that some S3 implementations can be rather slow with raw `ListObjectsV2` calls.  
By default, `ListObjectsV2` lists objects in alphabetical order. It seems that some implementations do not keep a sorted tree of objects, and have to generate this sorting ad-hoc.

This tool lists objects in a breadth-first fashion, as if they were a filesystem tree. Accordingly, this tool _may_ perform better for implementations that use a file storage underneath. Conversely, with a decent S3 implementation, this tool will likely be slower than the naive loop, unless your bucket contains a shallow "directory" structure.

See `list_all_objects --help` for usage.
