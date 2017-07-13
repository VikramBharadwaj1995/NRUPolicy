#include "Cache.h"
#include "System.h"
namespace mem
{
const misc::StringMap Cache::ReplacementPolicyMap =
{
	{ "LRU", ReplacementLRU },
	{ "FIFO", ReplacementFIFO },
	{ "Random", ReplacementRandom },
	{ "NRU", ReplacementNRU }
};
const misc::StringMap Cache::WritePolicyMap =
{
	{ "WriteBack", WriteBack },
	{ "WriteThrough", WriteThrough }
};
const misc::StringMap Cache::BlockStateMap =
{
	{ "N", BlockNonCoherent },
	{ "M", BlockModified },
	{ "O", BlockOwned },
	{ "E", BlockExclusive },
	{ "S", BlockShared },
	{ "I", BlockInvalid }
};
Cache::Cache(const std::string &name,
		unsigned num_sets,
		unsigned num_ways,
		unsigned block_size,
		ReplacementPolicy replacement_policy,
		WritePolicy write_policy)
		:
		name(name),
		num_sets(num_sets),
		num_ways(num_ways),
		block_size(block_size),
		replacement_policy(replacement_policy),
		write_policy(write_policy)
{
	// Derived fields
	assert(!(num_sets & (num_sets - 1)));
	assert(!(num_ways & (num_ways - 1)));
	assert(!(block_size & (block_size - 1)));
	num_blocks = num_sets * num_ways;
	log_block_size = misc::LogBase2(block_size);
	block_mask = block_size - 1;

	// Allocate blocks and sets
	blocks = misc::new_unique_array<Block>(num_blocks);
	sets = misc::new_unique_array<Set>(num_sets);
	
	// Initialize sets and blocks
	for (unsigned set_id = 0; set_id < num_sets; set_id++)
	{
		Set *set = getSet(set_id);
		for (unsigned way_id = 0; way_id < num_ways; way_id++)
		{
			Block *block = getBlock(set_id, way_id);
			block->way_id = way_id;
			set->lru_list.PushBack(block->lru_node);
		}
	}
}

void Cache::DecodeAddress(unsigned address,
		unsigned &set_id,
		unsigned &tag,
		unsigned &block_offset) const
{
	set_id = (address >> log_block_size) % num_sets;
	tag = address & ~block_mask;
	block_offset = address & block_mask;
}

bool Cache::FindBlock(unsigned address,
		unsigned &set_id,
		unsigned &way_id,
		BlockState &state) const
{
	// Get set and tag
	set_id = (address >> log_block_size) % num_sets;
	unsigned tag = address & ~block_mask;

	// Find block
	for (way_id = 0; way_id < num_ways; way_id++)
	{
		Block *block = getBlock(set_id, way_id);
		if (block->tag == tag && block->state != BlockInvalid)
		{
			state = block->state;
			return true;
		}
	}

	// Block not found
	set_id = 0;
	way_id = 0;
	state = BlockInvalid;
	return false;
}

void Cache::setBlock(unsigned set_id,
		unsigned way_id,
		unsigned tag,
		BlockState state)
{
	// Trace
	System::trace << misc::fmt("mem.set_block cache=\"%s\" "
			"set=%d way=%d tag=0x%x state=\"%s\"\n",
			name.c_str(),
			set_id,
			way_id,
			tag,
			BlockStateMap[state]);
	
	// Get set and block
	Set *set = getSet(set_id);
	Block *block = getBlock(set_id, way_id);

	// If the block is being brought to the cache now for the first time,
	// update the FIFO list.
	if (replacement_policy == ReplacementFIFO
			&& block->tag != tag)
	{
		set->lru_list.Erase(block->lru_node);
		set->lru_list.PushFront(block->lru_node);
		block->setReferencedBit();
	}

	// If the block is being brought in to the Cache for the first time, 
	// update th NRU list.
	/*if (replacement_policy == ReplacementPolicyNRU)
	{
		set->nru_list.Erase(block->nru_node);
		set->nru_list.PushFront(block->nru_node);
		block->setReferencedBit();
	}*/
	if (replacement_policy == ReplacementNRU)
	{
		set->lru_list.Erase(block->lru_node);
		set->lru_list.PushFront(block->lru_node);
		block->setReferencedBit();
	}

	// Set new values for block
	block->tag = tag;
	block->state = state;
}

void Cache::getBlock(unsigned set_id,
		unsigned way_id,
		unsigned &tag,
		BlockState &state) const
{
	Block *block = getBlock(set_id, way_id);
	tag = block->tag;
	state = block->state;
}

void Cache::AccessBlock(unsigned set_id, unsigned way_id)
{
	// Get set and block
	Set *set = getSet(set_id);
	Block *block = getBlock(set_id, way_id);

	// A block is moved to the head of the list for LRU policy. It will also
	// be moved if it is its first access for FIFO policy, i.e., if the
	// state of the block was invalid.
	bool move_to_head = replacement_policy == ReplacementLRU ||
			(replacement_policy == ReplacementFIFO
			&& block->state == BlockInvalid);
	
	// Move to the head of the LRU list
	if (move_to_head)
	{
		set->lru_list.Erase(block->lru_node);
		set->lru_list.PushFront(block->lru_node);
		if(block->referencedBit==0)
			block->setReferencedBit();
	}
	else
	{
		set->lru_list.Erase(block->lru_node);
		set->lru_list.PushFront(block->lru_node);
	        if(block->referencedBit==0)
			block->setReferencedBit();	
	}
}	

unsigned Cache::ReplaceBlock(unsigned set_id)
{
	// Get the set
	Set *set = getSet(set_id);

	// For LRU and FIFO replacement policies, return the block at the end of
	// the block list in the set.
	if (replacement_policy == ReplacementLRU ||
			replacement_policy == ReplacementFIFO)
	{
		// Get block at the end of LRU list
		Block *block = misc::cast<Block *>(set->lru_list.Back());
		assert(block);

		// Move it to the head to avoid making it a candidate in the
		// next call to getReplacementBlock().
		set->lru_list.Erase(block->lru_node);
		set->lru_list.PushFront(block->lru_node);

		// Return way index of the selected block
		return block->way_id;
	}
	
	// For NRU replacement policy, return the block at the end of
	// the block list in the set.

	else if(replacement_policy == ReplacementNRU)
	{
		// Get the Block which has referencedBit = 0 and (!BlockModified)
		Block *block = misc::cast<Block *>(set->lru_list.Back());
		assert(block);

		if(block->referencedBit==0 && (block->state == BlockModified))
		{
			set->lru_list.Erase(block->lru_node);
			set->lru_list.PushFront(block->lru_node);
		}
		else if(block->referencedBit==0 && (block->state != BlockModified))
		{
			set->lru_list.Erase(block->lru_node);
			set->lru_list.PushFront(block->lru_node);
		}
		else if(block->referencedBit==1 && (block->state == BlockModified))
		{
			set->lru_list.Erase(block->lru_node);
			set->lru_list.PushFront(block->lru_node);
		}
		else
		{
			set->lru_list.Erase(block->lru_node);
			set->lru_list.PushFront(block->lru_node);
		}
	}	
	// Random replacement policy
	assert(replacement_policy == ReplacementRandom);
	return random() % num_ways;
}
}  // namespace mem
