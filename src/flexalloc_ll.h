//Copyright (C) 2021 Joel Granados <j.granados@samsung.com>

#ifndef __FLEXALLOC_LL_H_
#define __FLEXALLOC_LL_H_
#include "flexalloc_mm.h"

#define FLA_LINKED_LIST_NULL INT32_MAX

/**
 * @brief Prepend a new slab header to a list that has only one head pointer
 *
 * The prepended slab header will always be first and head pointer
 * will always point to it.
 *
 * @param fs flexalloc system handle
 * @param slab slab header to prepend
 * @param head pointer to slab head list id
 * @return zero on success. non zero otherwise
 */
int
fla_hdll_prepend(struct flexalloc * fs, struct fla_slab_header * slab, uint32_t *head);

/**
 * @brief Remove slab from a list that has only one head pointer
 *
 * @param fs flexalloc system handle
 * @param slab slab header to be removed
 * @param head pointer to slab head list id
 * @return zeor on success. non zero otherwise
 */
int
fla_hdll_remove(struct flexalloc * fs, struct fla_slab_header * slab, uint32_t * head);

/**
 * @brief Remove all slabs from list starting at head
 *
 * @param fs flexalloc system handle
 * @param head pointer to first ID in list.
 *        Gets modified to FLA_LINKED_LIST_NULL on success
 * @param execute_on_release Function to be executed once the slab is removed
 * @return zero on success. non zero otherwise
 */
int
fla_hdll_remove_all(struct flexalloc *fs, uint32_t *head,
                    int (*execute_on_release)(struct flexalloc *fs, struct fla_slab_header*));

/**
 * @brief Remove slab from a list that has head and tail pointers
 *
 * Remove slab from the head of the doubly linked list
 *
 * @param fs flexalloc system handle
 * @param head pointer to where the head ID is
 * @param tail pointer to where the tail ID is
 * @param {name} slab header where to put the slab that is being removed
 * @return zero on success. non zero otherwise
 */
int
fla_edll_remove_head(struct flexalloc * fs, uint32_t * head, uint32_t * tail,
                     struct fla_slab_header ** a_slab);

/**
 * @brief Append slab to list that has a head and tail pointers
 *
 * Always adds to tail
 *
 * @param fs flexalloc system handle
 * @param head pointer to where the head ID is
 * @param tail pointer to where the tail ID is
 * @param a_slab slab header to return to list
 * @return zero on success. non zero otherwise
 */
int
fla_edll_add_tail(struct flexalloc *fs, uint32_t * head, uint32_t * tail,
                  struct fla_slab_header * a_slab);


#endif // __FLEXALLOC_LL_H_
