# real-time statistical waveset processing

inspired deeply by [nathan ho's brilliant post on statistical waveset processing](https://nathan.ho.name/posts/wavesets-clustering/), this project aims to implement a real-time version of the procedure in an audio plugin. i deeply suggest you read his original blog post for more information about the exact approach here.

to handle the real-time statistical analysis, i used the [RTEFC (real-time exponential filter clustering)](https://gregstanleyandassociates.com/whitepapers/BDAC/Clustering/clustering.htm) algorithm, which mainly keeps track of the centroids of each cluster -- exactly what we want for this use case. as nathan points out, doing a shoddy sort of k-means algorithm and discarding the oldest samples could probably work too, but i like things with acronyms.

sources:
https://nathan.ho.name/posts/wavesets-clustering/
https://gregstanleyandassociates.com/whitepapers/BDAC/Clustering/clustering.htm
