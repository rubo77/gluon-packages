#include <math.h>

#include "dhcp.h"
#include "block.h"
#include "logger.h"

int block_own( ddhcp_block *block ) {
  block->state = DDHCP_OURS;
  // TODO Have a preallocated list of dhcp_lease_blocks?
  block->addresses = (struct dhcp_lease*) calloc(sizeof(struct dhcp_lease),block->subnet_len);

  if ( block->addresses != NULL ) {
    return 1;
  }

  for ( unsigned int index = 0; index < block->subnet_len; index++ ) {
    block->addresses[index].state = FREE;
    block->addresses[index].lease_end = 0;
  }

  return 0;
}

void block_free( ddhcp_block *block ) {
  DEBUG("block_free(%i)\n",block->index);
  block->state = DDHCP_FREE;

  if ( block->addresses ) {
    DEBUG("Free DHCP leases for Block%i\n",block->index);
    free(block->addresses);
  }
}

ddhcp_block* block_find_free(ddhcp_block *blocks, ddhcp_config *config) {
  DEBUG("block_find_free(blocks,config)\n");
  ddhcp_block *block = blocks;

  ddhcp_block_list free_blocks, *tmp;
  INIT_LIST_HEAD(&free_blocks.list);
  uint32_t num_free_blocks = 0;

  for( uint32_t i = 0 ; i < config->number_of_blocks; i++ ) {
    if( block->state == DDHCP_FREE ) {
      tmp = (ddhcp_block_list*) malloc( sizeof ( ddhcp_block_list ) );
      tmp->block = block;
      list_add_tail((&tmp->list), &(free_blocks.list) );
      num_free_blocks++;
    }

    block++;
  }

  DEBUG("block_find_free(...): found %i free blocks\n",num_free_blocks);

  ddhcp_block *random_free = NULL;
  int r = -1;

  if ( num_free_blocks > 0 ) {
    r = rand() % num_free_blocks;
  }

  struct list_head *pos, *q;

  list_for_each_safe(pos, q, &free_blocks.list) {
    tmp = list_entry(pos, ddhcp_block_list, list);
    block = tmp->block;
    list_del(pos);
    free(tmp);

    if(r == 0) {
      random_free = block;
    }

    r--;
  }

  DEBUG("block_find_free(...)-> block %i\n",random_free->index);
  return random_free;
}

int block_claim( ddhcp_block *blocks, int num_blocks , ddhcp_config *config ) {
  DEBUG("block_claim(blocks, %i, config)\n",num_blocks);
  // Handle blocks already in claiming prozess
  struct list_head *pos, *q;
  time_t now = time(NULL);

  list_for_each_safe(pos,q,&(config->claiming_blocks).list) {
    ddhcp_block_list *tmp = list_entry(pos, ddhcp_block_list, list);
    ddhcp_block *block = tmp->block;

    if ( block->claiming_counts == 3 ) {
      block_own(block);
      INFO("Block %i claimed after 3 claims.\n",block->index);
      num_blocks--;
      list_del(pos);
      config->claiming_blocks_amount--;
      free(tmp);
    } else if ( block->state != DDHCP_CLAIMING ) {
      DEBUG("block_claim(...): block %i is no longer marked as claiming\n",block->index);
      list_del(pos);
      config->claiming_blocks_amount--;
      free(tmp);
    }
  }

  // Do we still need more, then lets find some.
  if ( (unsigned int) num_blocks > config->claiming_blocks_amount ) {
    // find num_blocks - config->claiming_blocks_amount free blocks
    int needed_blocks = num_blocks - config->claiming_blocks_amount;

    for ( int i = 0 ; i < needed_blocks ; i++ ) {
      ddhcp_block *block = block_find_free( blocks, config );

      if ( block != NULL ) {
        ddhcp_block_list* list = (ddhcp_block_list*) malloc( sizeof ( ddhcp_block_list ) );
        block->state = DDHCP_CLAIMING;
        block->claiming_counts = 0;
        block->timeout = now + config->tentative_timeout;
        list->block = block;
        list_add_tail(&(list->list), &(config->claiming_blocks.list));
        config->claiming_blocks_amount++;
      } else {
        // We are short on free blocks in the network.
        WARNING("Warning: Network has no free blocks left!\n");
        // TODO In a feature version we could start to forward DHCP requests
        //      to other servers.
      }
    }
  }

  // TODO Sort blocks in claiming process by number of claims already processed.

  // TODO If we have more blocks in claiming process than we need, drop the tail
  //      of blocks for which we had less claim announcements.

  // Send claim message for all blocks in claiming process.
  struct ddhcp_mcast_packet packet;
  packet.node_id = config->node_id;
  memcpy(&(packet.prefix),&config->prefix,sizeof(struct in_addr));
  packet.prefix_len = config->prefix_len;
  packet.blocksize = config->block_size;
  packet.command = 2;
  packet.count = config->claiming_blocks_amount;
  packet.payload = (struct ddhcp_payload*) malloc( sizeof(struct ddhcp_payload) * config->claiming_blocks_amount);
  int index = 0;
  list_for_each(pos,&(config->claiming_blocks).list) {
    ddhcp_block_list  *tmp = list_entry(pos, ddhcp_block_list, list);
    ddhcp_block *block = tmp->block;
    block->claiming_counts++;
    packet.payload[index].block_index = block->index;
    packet.payload[index].timeout = 0;
    packet.payload[index].reserved = 0;
    index++;
  }

  if( packet.count > 0 ) {
    send_packet_mcast( &packet , config->mcast_socket, config->mcast_scope_id );
  }

  free(packet.payload);
  return 0;
}

int block_num_free_leases( ddhcp_block *block, ddhcp_config *config ) {
  DEBUG("block_num_free_leases(blocks, config)\n");
  int free_leases = 0;

  for ( uint32_t i = 0 ; i < config->number_of_blocks ; i++ ) {
    if ( block->state == DDHCP_OURS ) {
      free_leases += dhcp_num_free( block );
    }

    block++;
  }

  DEBUG("block_num_free_leases(...)-> Found %i free dhcp leases in OUR blocks\n",free_leases);
  return free_leases;
}

void block_update_claims( ddhcp_block *blocks, int blocks_needed, ddhcp_config *config ) {
  DEBUG("block_update_claims(blocks, %i, config)\n",blocks_needed);
  int our_blocks = 0;
  ddhcp_block *block = blocks;
  time_t now = time(NULL);
  int timeout_half = floor( (double) config->block_timeout / 2 );
  int blocks_needed_tmp = blocks_needed;

  // TODO Use a linked list instead of processing the block list twice.
  for ( uint32_t i = 0 ; i < config->number_of_blocks ; i++ ) {
    if ( block->state == DDHCP_OURS && block->timeout < now + timeout_half ) {
      if ( blocks_needed_tmp < 0 && dhcp_num_free ( block ) == config->block_size ) {
        DEBUG("block_update_claims(...): block %i no longer needed\n", block->index);
        blocks_needed_tmp--;
        block_free( block );
      } else {
        our_blocks++;
      }
    }

    block++;
  }

  if( our_blocks == 0 ) {
    DEBUG("block_update_claims(...)-> No blocks need claim update.\n");
    return;
  }

  struct ddhcp_mcast_packet packet;

  packet.node_id = config->node_id;

  memcpy(&packet.prefix,&config->prefix,sizeof(struct in_addr));

  packet.prefix_len = config->prefix_len;

  packet.blocksize = config->block_size;

  packet.command = 1;

  packet.count = our_blocks;

  packet.payload = (struct ddhcp_payload*) malloc(sizeof(struct ddhcp_payload) * our_blocks);

  int index = 0;

  block = blocks;

  for ( uint32_t i = 0 ; i < config->number_of_blocks ; i++ ) {
    if ( block->state == DDHCP_OURS && block->timeout < now + timeout_half ) {
      packet.payload[index].block_index = block->index;
      packet.payload[index].timeout     = config->block_timeout;
      packet.payload[index].reserved    = 0;
      index++;
      block->timeout = now + config->block_timeout;
      DEBUG("block_update_claims(...): update claim for block %i\n",block->index);
    }

    block++;
  }

  if( packet.count > 0 ) {
    send_packet_mcast( &packet , config->mcast_socket, config->mcast_scope_id );
  }

  free(packet.payload);
}

void block_check_timeouts( ddhcp_block *blocks, ddhcp_config *config ) {
  DEBUG("block_check_timeouts(blocks, config)\n");
  ddhcp_block *block = blocks;
  time_t now = time(NULL);

  for ( uint32_t i = 0 ; i < config->number_of_blocks ; i++ ) {
    if ( block->timeout < now && block->state != DDHCP_BLOCKED && block->state != DDHCP_FREE ) {
      INFO("Block %i FREE throught timeout.\n",block->index);
      block_free(block);
    }

    if ( block->state == DDHCP_OURS ) {
      dhcp_check_timeouts( block );
    }

    block++;
  }
}

void block_free_claims( ddhcp_config *config ) {
  if ( ! list_empty(&config->claiming_blocks.list) ) {
    struct list_head *pos, *q;
    list_for_each_safe(pos,q,&(config->claiming_blocks).list) {
      ddhcp_block_list *tmp = list_entry(pos, ddhcp_block_list, list);
      list_del(pos);
      free(tmp);
    }
  }
}

void block_show_status( int fd, ddhcp_block *blocks,  ddhcp_config *config ) {
  ddhcp_block *block = blocks;
  dprintf(fd, "index,state,owner,claim_count,timeout\n");
  for ( uint32_t i = 0 ; i < config->number_of_blocks ; i++ ) {
    dprintf(fd, "%i,%i,,%u,%lu\n",block->index,block->state,block->claiming_counts,block->timeout);
    block++;
  }
}
