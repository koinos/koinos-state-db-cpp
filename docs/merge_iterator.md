## Merge Iterator

This is a design document for an updated implementation of the merge iterator. The current merge iterator is an asymmetric implementation (incrementing and decrementing are extremely different algorithms). This document describes an alternative implementation for the merge iterator that would make the merge iterator algorithms much closer to symmetric (at the very least, the principles in each function are the same).

### StateDB Design

StateDB is designed as a root (irreversible) state with positive deltas built on top. If these positive deltas were to be applied, the result would be the state at that specific delta.

Looking up a specific key is easy. Starting at the head state, look up the key. If it exists, that is guaranteed to be the newest value for the key and if the key is explicitly deleted, then it does not exist. In the worst case scenario, all deltas down to the root need to be checked.

### Iteration

Iterating over keys is much more complicated as consecutive keys could exist in different deltas. This is where the merge iterator comes in. The basic premise is similar to what occurs during the second stage of a merge sort. Each of our deltas are already sorted and so we can do a similar merge. Consider the following states:

```
 v         v         v
|b|e|f|   |a|c|h|   |d|g|i|
```

Just like in a merge sort, we can look at the smallest key in each delta and choose it. In this case, it is `a`. We grab `a` and then increment the iterator on that delta. We will also use an `*` to denote which iterator is first, which indicates the key that the merge iterator as a whole resolves to.

```
 v*          v       v
|b|e|f|   |a|c|h|   |d|g|i|
```

We then compare the iterators and see that `b` is next, grab `b` and increment that iterator.

It is trivial to show that this works for n lists, so we refer to this as an n-way merge sort.

### Conflicts

The complication comes in when we begin dealing with the order of the deltas. In this example, there is no overlap in keys, but in reality, keys can conflict between deltas. When they do, we resolve the conflict with the highest revision (most recent) delta. We call a key that has a conflict with a later delta dirty.

The function to determine if a key is dirty is easy. We iterate over deltas from the last to the current and check if the key was modified in that delta. We consider a key modified if the value was changed or the key was deleted. If we get back to our delta without finding a modification, then the key is not dirty. Modifications at earlier deltas do not make the key dirty because our delta is the most recent modification.

We can extend our previous diagram to show this:

```
    v
1: |b|c|d|f|

    v*
2: |a|c|e|h|

    v
3: |d|g|i|
```

In this scenario we will first get `a` from delta 2 and increment the iterator to `c`. Then grab `b` from delta 1. Now we have a problem because the iterators on deltas 1 and 2 both point to `c`. We resolve the tie by with the later iterator winning. In this case `c`. The next time we iterate we increment delta 2's iterator to `d`, but now delta 1's iterator is still at `c` and the tie has been resolved.

What we would like is for the iterators to always point to a valid key. We can add a step. When we increment iterators, we can check if the next key is dirty or not. If it is, we increment that iterator and check again.

If we were to do that, after we increment delta 2's `c` to `d`, we would see 1's 'c' as the next key. We check if the key is dirty, which it is because the key is also in delta 2 and we increment it to `e`. Now delta 3's `d` is the next lowest key, which is the correct result.

We can extend this one step further to consider removed keys. If delta 3 removed key `c`, this would have made delta 2's `c` also dirty. Let's rewind our example and see what would have happened.

```
    v*
1: |b|c|d|f|

      v
2: |a|c|e|h|

    v
3: |d|g|i|
R  |c|
```

When we increment the merge iterator we increment 1's iterator to `c`. 2's `c` is now next and we check if it is dirty. It is because `c` was removed in delta 3, so we increment 2 to `d`.

```
      v
1: |b|c|d|f|

        v
2: |a|c|e|h|

    v
3: |d|g|i|
R  |c|
```

1's `c` is next so we check if it is dirty. It is also dirty because 3 removed `c`. Also 2 modifies `c`, but the removal from 3 is enough to know the key is dirty. So we increment 1 to `e`. The merge iterator state now looks like this:

```
        v
1: |b|c|d|f|

        v
2: |a|c|e|h|

    v*
3: |d|g|i|
R  |c|
```

Delta 3's `d` is the next key, which is correct.

### Decrement

Now let's consider decrementing the merge iterator. We are in the state where delta 1 points to `d`, 2 to `e` and 3 to `d`. How do we unwind? From this example, we know we need to get delta 1 back to `b`. We could try to reverse our steps, but how do we know to reverse 1's `d` when it isn't the highest or lowest iterator? (The current order is 3, 1, 2). This is a simple example and while we could come up with an algorithm that reverse the current state, there are many corner cases to consider, which makes it complicated.

Instead, what if we had an iterator to the last known good key for each delta. If we sorted those by key in the opposite direction, we would have a set of iterators that lets us go backwards using the same general algorithm. Thankfully, reverse iterators do exactly what we want to do.

As a note on notation, we are now going to add the end sentinel keys `end` and `rend` in the lists (Denoted with `X`). This is because having a forward or reverse iterator pointing to these keys becomes very important.

We are also going to show the forward iterator with `F` on top of the list and the reverse iterator with `R` on the bottom of the list. We will also use an `*` to denote which of the reverse iterators is the front of their list, but we only use the first forward iterator when determining the value of the merge iterator as a whole. The initial state looks like this:

```
     F
1: X|b|c|d|f|X
   R

     F*
2: X|a|c|e|h|X
   R

     F
3: X|d|g|i|X   Rm: |c|
   R*
```

This is a bit more complicated, but this diagram will be crucial moving forward. Like I said earlier, the reverse iterators point to the last know good keys. When we start at the beginning, there are no last good keys.

It is also worth noting that both the forward and reverse iterators are actually pointing to the same keys. This is because there is no such thing as `rend`. It is actually just `begin`, but the reverse iterator offsets the key by one. We will always show the logical value of the iterator, but this offset is important when having to convert between forward and reverse iterators. See this documentation for more information: https://en.cppreference.com/w/cpp/iterator/reverse_iterator

The forward iteration algorithm is the same except that we set the reverse iterator to the forward iterator before doing to first increment. This would move 2's `R` to `a` and then increment `a`.

```
     F*
1: X|b|c|d|f|X
   R

       F
2: X|a|c|e|h|X
     R*

     F
3: X|d|g|i|X   Rm: |c|
   R
```

Let's follow a few more increments until we reach our complicated state from earlier.

We increment again, this takes 1's `R` to `b` and increments 1's `F` to `c`. 2's `c` is now first and is dirty so it is incremented to `e`. 1's `c` is now first and is also dirty, so it is incremented to `d`. We are left with the following state:

```
         F
1: X|b|c|d|f|X
     R*

         F
2: X|a|c|e|h|X
     R

     F*
3: X|d|g|i|X   Rm: |c|
   R
```

Notice that while 1 and 2's forward iterators were both incremented, only 2's `R` was incremented to `b` because that was the merge iterator key at the start of the increment. The transient increments due to dirty heads do not update reverse iterators.

To decrement the iterator, we do the opposite of the increment. We take the head reverse iterator and move the forward iterator to it. And then we increment (move backwards because it is a reverse iterator) the reverse iterator and resolve dirty values as we would for forward iteration.

In this scenario, this means we take 1's `F` and move it to `b`. We then increment 1's `R` to `rend`. 2's `R` is at `a` which is not dirty, so we are done.

```
     F*
1: X|b|c|d|f|X
   R

         F
2: X|a|c|e|h|X
     R*

     F*
3: X|d|g|i|X   Rm: |c|
   R
```

Notice, this is not actually a complete reversal of the increment operation (2's `F` is at `e` instead of `c`.), but the end state is the same. It might not be obvious why this works in both directions, but if you consider the scenario where we start at `end` and decrement to the last key, it will become obvious why the symmetric decrement operation works.

## Lower Bound

We now have generic algorithms for incrementing and decrementing a merge iterator from `begin` and `end`. We also need to initialize a merge iterator using `lower_bound` and `upper_bound`.

The first thing we could try is simply calling `lower_bound` on all of the forward and reverse iterators and see where that gets us. For this example, let's call `lower_bound` on `c`. The merge state has keys for `a` through `i`, but not `c` because `c` was removed. So the merge iterator should start with `b` and be able to continue iterating in either direction correctly.

If we do that, we get the following internal state:

```
       F
1: X|b|c|d|f|X
       R

       F*
2: X|a|c|e|h|X
       R*

           F
3: X|d|g|i|X   Rm: |c|
   R
```

There are a couple of big problems.

The first is that the merge iterator is pointing to `c`. Even if we resolved dirty keys, we would be left with 1 pointing to `d` and 2 pointing to `e`, which is not the right answer. It isn't even a lower bound of `c`. Furthermore, 3's `F` is all the way at `end` because not of its members were less than `c`. No matter what we do, we will never iterate to a value in 3.

Rather, what we want to do is have the forward iterators call `upper_bound` and then create reverse iterators on those values. This will create reverse iterators that point to a value one less than the call to upper bound. If we then do our dirty check on the reverse iterators once, we can set the forward iterator to the first reverse iterator and that will be our lower bound. Last, we increment that reverse iterator once more and we are done.

Let's look at this in discrete steps.

First, we call `upper_bound` on `c` for all deltas and set reverse iterators to the same iterator, which is logically one to the left.

```
         F
1: X|b|c|d|f|X
       R

         F
2: X|a|c|e|h|X
       R*

     F*
3: X|d|g|i|X   Rm: |c|
   R
```

We then do our dirty check on the reverse iterators until we have a non-dirty value. 2's `c` is dirty so we increment it to `a`. Then 1's `c` is also dirty so we increment it to `b`. `b` is not dirty.

```
         F
1: X|b|c|d|f|X
     R*

         F
2: X|a|c|e|h|X
     R

     F*
3: X|d|g|i|X   Rm: |c|
   R
```

Lastly, we take 1's `F` and move it to `b` and increment `R` to `rend`.

```
     F*
1: X|b|c|d|f|X
   R

         F
2: X|a|c|e|h|X
     R*

     F
3: X|d|g|i|X   Rm: |c|
   R
```

Let's make sure an increment and decrement work as expected.

To increment, we take 1's `F`, setting `R` to it and increment the key. It increments to `c` and is still first. 1's `c` is dirty, so we increment it again to `d`. `d` is the next key.

```
         F
1: X|b|c|d|f|X
     R*

         F
2: X|a|c|e|h|X
     R

     F*
3: X|d|g|i|X   Rm: |c|
   R
```

Likewise, to decrement we take 2's `R`, setting `F` to it and increment the key. It increments to `rend` and 2's `F` points to `a` and becomes head.

```
     F
1: X|b|c|d|f|X
   R

     F*
2: X|a|c|e|h|X
   R

     F
3: X|d|g|i|X   Rm: |c|
   R*
```

## Upper Bound

Upper bound and lower bound are not symmetric, but complementary. The algorithms are not identical, but similar.

For upper bound, we take the upper bound of the key again. Again, let's use `c`.

```
         F
1: X|b|c|d|f|X
       R

         F
2: X|a|c|e|h|X
       R*

     F*
3: X|d|g|i|X   Rm: |c|
   R
```

Then, instead of setting `F` to any value, we do the dirty checks for both `F` and `R`.

```
         F
1: X|b|c|d|f|X
     R

         F
2: X|a|c|e|h|X
     R*

     F*
3: X|d|g|i|X   Rm: |c|
   R
```

With these simple steps, we now have `F` pointing to the correct upper bound and `R` pointing to the last valid key, ready for decrementing.
