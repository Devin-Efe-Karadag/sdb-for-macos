#include <libsdb/detail/arm64_abi.hpp>
namespace {
bool collect(sdb::type value,std::size_t base,sdb::arm64_hfa& hfa,unsigned depth){
    if(depth>16)return false;

    if(!value.is_from_dwarf()){
        if(value.get_builtin_type()!=sdb::builtin_type::floating_point)return false;

        if(hfa.element_size&&hfa.element_size!=8)return false;
        hfa.element_size=8;hfa.offsets.push_back(base);return hfa.offsets.size()<=4;
    }
    value=value.strip_cv_typedef();auto die=value.get_die();auto tag=die.abbrev_entry()->tag;

    if(tag==DW_TAG_base_type){
        if(!die.contains(DW_AT_encoding)||die[DW_AT_encoding].as_int()!=DW_ATE_float)return false;

        auto size=value.byte_size();if(size!=4&&size!=8)return false;

        if(hfa.element_size&&hfa.element_size!=size)return false;
        hfa.element_size=size;hfa.offsets.push_back(base);return hfa.offsets.size()<=4;
    }

    if(tag==DW_TAG_array_type){
        auto element=die[DW_AT_type].as_type();auto size=element.byte_size();if(!size||value.byte_size()/size>4)return false;

        for(std::size_t i=0;i<value.byte_size()/size;++i)if(!collect(element,base+i*size,hfa,depth+1))return false;return true;
    }

    if(tag!=DW_TAG_structure_type&&tag!=DW_TAG_class_type)return false;

    if(value.is_non_trivial_for_calls())return false;

    for(auto child:die.children()){
        if(child.abbrev_entry()->tag==DW_TAG_member&&child.contains(DW_AT_data_member_location)){
            if(child.contains(DW_AT_bit_size)||!collect(child[DW_AT_type].as_type(),base+child[DW_AT_data_member_location].as_int(),hfa,depth+1))return false;
        }else if(child.abbrev_entry()->tag==DW_TAG_inheritance)return false;
    }return !hfa.offsets.empty()&&hfa.offsets.size()<=4;
}
}
std::optional<sdb::arm64_hfa> sdb::classify_arm64_hfa(type value){arm64_hfa hfa;if(!collect(value,0,hfa,0))return std::nullopt;return hfa;}
