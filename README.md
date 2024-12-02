# raygui-fun

This is just a fun little repo for me to mess around with raygui. It started with wanting to mess around with BK-trees for other ideas, so it includes a full Damerau-Levenshtein distance calculation and functions to build a BK-tree off a provided word dictionary. Also allows serializing/deserializing tree to file.

Currently, if you re-index or re-load the tree from a file, the subsequent indexes/loads are massively slow. It goes away if I remove `free(node->word)` from `void freeNode(Node *node)`, not sure whats going on there.
