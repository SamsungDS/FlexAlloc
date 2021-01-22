//Copyright (C) 2021 Joel Granados <j.granados@samsung.com>

#include "flexalloc_ll.h"
#include "flexalloc_util.h"

int
fla_hdll_prepend(struct flexalloc * fs, struct fla_slab_header * slab, uint32_t *head)
{
  int err;
  uint32_t slab_id;
  struct fla_slab_header * head_slab = NULL;

  err = fla_slab_id(slab, fs, &slab_id);
  if(FLA_ERR(err, "fla_slab_id()"))
  {
    goto exit;
  }

  if(*head != FLA_LINKED_LIST_NULL)
  {
    head_slab = fla_slab_header_ptr(*head, fs);
    if((err = FLA_ERR(!head_slab, "fla_slab_header_ptr()")))
    {
      goto exit;
    }
    head_slab->prev = slab_id;
  }

  slab->next = *head;
  slab->prev = FLA_LINKED_LIST_NULL;
  *head = slab_id;

exit:
  return err;
}

int
fla_hdll_remove(struct flexalloc * fs, struct fla_slab_header * slab, uint32_t * head)
{
  int err = 0;
  struct fla_slab_header * temp_slab;

  // Remove from head
  if(slab->prev == FLA_LINKED_LIST_NULL)
  {
    *head = slab->next;
  }
  else
  {
    temp_slab = fla_slab_header_ptr(slab->prev, fs);
    if((err = FLA_ERR(!temp_slab, "fla_slab_header_ptr()")))
    {
      goto exit;
    }
    temp_slab->next = slab->next;
  }

  if(slab->next != FLA_LINKED_LIST_NULL)
  {
    temp_slab = fla_slab_header_ptr(slab->next, fs);
    if((err = FLA_ERR(!temp_slab, "fla_slab_header_ptr()")))
    {
      goto exit;
    }
    temp_slab->prev = slab->prev;
  }

exit:
  return err;
}

int
fla_hdll_remove_all(struct flexalloc *fs, uint32_t *head,
                    int (*execute_on_release)(struct flexalloc *fs, struct fla_slab_header*))
{
  int err = 0;
  struct fla_slab_header * curr_slab;

  for(uint32_t i = 0 ; i < fs->geo.nslabs && *head != FLA_LINKED_LIST_NULL; ++i)
  {
    curr_slab = fla_slab_header_ptr(*head, fs);
    if((err = FLA_ERR(!curr_slab, "fla_slab_header_ptr()")))
    {
      goto exit;
    }

    *head = curr_slab->next;

    err = execute_on_release(fs, curr_slab);
    if(FLA_ERR(err, "execute_on_release()"))
    {
      goto exit;
    }
  }

  // FIXME: This should probably be an assert
  err = FLA_ERR(*head != FLA_LINKED_LIST_NULL, "fla_hdll_remove_all()");

exit:
  return err;
}


int
fla_edll_remove_head(struct flexalloc * fs, uint32_t * head, uint32_t * tail,
                     struct fla_slab_header ** a_slab)
{
  int err;
  struct fla_slab_header * new_head;

  err = !head || !tail;
  if(FLA_ERR(err, "fla_edll_remove()"))
  {
    goto exit;
  }

  *a_slab = fla_slab_header_ptr(*head, fs);
  if((err = -FLA_ERR(!(*a_slab), "fla_slab_header_ptr()")))
  {
    goto exit;
  }

  if (*head == *tail)
  {
    *head = FLA_LINKED_LIST_NULL;
    *tail = FLA_LINKED_LIST_NULL;
  }
  else
  {
    new_head = fla_slab_header_ptr((*a_slab)->next, fs);
    if((err = -FLA_ERR(!(new_head), "fla_slab_header_ptr()")))
    {
      goto exit;
    }

    *head =  (*a_slab)->next;
    new_head->prev = FLA_LINKED_LIST_NULL;
  }

exit:
  return err;
}

int
fla_edll_add_tail(struct flexalloc *fs, uint32_t * head, uint32_t * tail,
                  struct fla_slab_header * r_slab)
{
  int err;
  uint32_t r_slab_id;

  err = fla_slab_id(r_slab, fs, &r_slab_id);
  if(FLA_ERR(err, "fla_slab_id()"))
  {
    goto exit;
  }

  r_slab->next = FLA_LINKED_LIST_NULL;
  if(*head == *tail && *head == FLA_LINKED_LIST_NULL)
  {
    *tail = r_slab_id;
    *head = r_slab_id;
    r_slab->prev = FLA_LINKED_LIST_NULL;
  }
  else
  {
    struct fla_slab_header * tail_slab;
    tail_slab = fla_slab_header_ptr(*tail, fs);
    if((err = FLA_ERR(!(tail_slab), "fla_slab_header_ptr()")))
    {
      goto exit;
    }

    tail_slab->next = r_slab_id;
    r_slab->prev = *tail;
    *tail = r_slab_id;
  }

exit:
  return err;
}
