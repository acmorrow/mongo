import yaml

import numpy as np

from matplotlib import pyplot as plt
from scipy.cluster.hierarchy import dendrogram
from sklearn.datasets import load_iris
from sklearn.cluster import AgglomerativeClustering


k_buildvariants_key = 'buildvariants'
k_tasks_key = 'tasks'

# Load up evergreen.yml
with open('etc/evergreen.yml') as evg_yaml:
    data = yaml.load(evg_yaml, Loader=yaml.FullLoader)

variants = data[k_buildvariants_key]

# Produce an ordered list of the known tasks
all_tasks = set()
for variant in variants:
    variant_tasks = variant[k_tasks_key]
    all_tasks.update(t['name'] for t in variant_tasks)
all_tasks = list(sorted(all_tasks))

variant_vectors = dict()
for variant in variants:
    variant_tasks = set(t['name'] for t in variant[k_tasks_key])
    variant_vectors[variant['name']] = [t in variant_tasks for t in all_tasks]

def plot_dendrogram(model, **kwargs):
    # Create linkage matrix and then plot the dendrogram

    # create the counts of samples under each node
    counts = np.zeros(model.children_.shape[0])
    n_samples = len(model.labels_)
    for i, merge in enumerate(model.children_):
        current_count = 0
        for child_idx in merge:
            if child_idx < n_samples:
                current_count += 1  # leaf node
            else:
                current_count += counts[child_idx - n_samples]
        counts[i] = current_count

    linkage_matrix = np.column_stack([model.children_, model.distances_,
                                      counts]).astype(float)

    # Plot the corresponding dendrogram
    dendrogram(linkage_matrix, **kwargs)


X = np.array([variant_vectors[key] for key in sorted(variant_vectors)])

# setting distance_threshold=0 ensures we compute the full tree.
model = AgglomerativeClustering(distance_threshold=0, n_clusters=None)

model = model.fit(X)
plt.title('Hierarchical Clustering Dendrogram')
# plot the top three levels of the dendrogram
plot_dendrogram(model, truncate_mode='level', p=50, orientation='left', labels=[k for k in sorted(variant_vectors)])
plt.ylabel("Variant")
plt.show()
