# Sample footage

Drop the video clips you want to analyze here.

The default configuration ([config/lifeguard.conf](../config/lifeguard.conf))
points at `samples/pool.mp4`:

```
video_file = samples/pool.mp4
```

Any file OpenCV can decode works (MP4/H.264 is the common case). Supply your
own clip and either name it `pool.mp4` or update `video_file` in the config.

Video files are ignored by git (see [.gitignore](../.gitignore)); only this
README is tracked.
