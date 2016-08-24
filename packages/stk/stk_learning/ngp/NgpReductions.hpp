#ifndef PACKAGES_STK_STK_LEARNING_KOKKOS_REDUCTIONS_H_
#define PACKAGES_STK_STK_LEARNING_KOKKOS_REDUCTIONS_H_

#include <stk_util/stk_config.h>
#include <Kokkos_Core.hpp>
#include <stk_util/util/StkVector.hpp>

namespace ngp {

template <typename T>
struct MinFunctor
{
    STK_FUNCTION
    void operator()(volatile T& update, volatile const T& input) const
    {
        update = update < input ? update : input;
    }
};

template <typename T>
struct MaxFunctor
{
    STK_FUNCTION
    void operator()(volatile T& update, volatile const T& input) const
    {
        update = update > input ? update : input;
    }
};

template <typename Mesh, typename Field>
struct FieldAccessFunctor
{
    typedef typename Field::value_type FieldData;

    STK_FUNCTION
    FieldAccessFunctor(const typename Mesh::BucketType *b, Field f) : bucket(b), field(f) { }

    STK_FUNCTION
    void operator()(int i, FieldData& update) const
    {
        FieldData value = field.const_get(typename Mesh::MeshIndex{bucket, static_cast<unsigned>(i)}, 0);
        update = value;
    }

private:
    const typename Mesh::BucketType *bucket;
    Field field;
};

template <typename Mesh, typename Field, typename ReductionOp>
struct ReductionTeamFunctor
{
    typedef typename Field::value_type FieldData;

    STK_FUNCTION
    ReductionTeamFunctor(const Mesh m, Field f, stk::Vector<unsigned> b, const FieldData &i) : mesh(m), field(f), bucketIds(b), initialValue(i) { }

    STK_FUNCTION
    void init(FieldData &update) const
    {
        update = initialValue;
        std::cerr << "MinTeamFunctor:init() update = " << update << std::endl;
    }

//    STK_FUNCTION
//    void join(volatile FieldData& update, volatile const FieldData& input) const
//    {
//        update = ReductionOp()(update, input);
//        std::cerr << "MinTeamFunctor:join() update = " << update << std::endl;
//    }

    typedef typename Kokkos::TeamPolicy<typename Mesh::MeshExecSpace, ngp::ScheduleType>::member_type TeamHandleType;
    STK_FUNCTION
    void operator()(const TeamHandleType& team, FieldData& update) const
    {
        std::cerr << "MinTeamFunctor:operator() update = " << update << std::endl;
        const int bucketIndex = bucketIds.device_get(team.league_rank());
        const typename Mesh::BucketType &bucket = mesh.get_bucket(field.get_rank(), bucketIndex);
        unsigned numElements = bucket.size();
        Kokkos::parallel_reduce(Kokkos::TeamThreadRange(team, 0u, numElements),
                                FieldAccessFunctor<Mesh, Field>(&bucket, field),
                                ReductionOp(), update);
    }

private:
    const Mesh mesh;
    Field field;
    stk::Vector<unsigned> bucketIds;
    const FieldData &initialValue;
};

template <typename Mesh, typename Field, typename ReductionOp>
typename Field::value_type get_field_reduction(Mesh &mesh, Field field, const stk::mesh::Selector &selector, const typename Field::value_type &initialValue)
{
    stk::Vector<unsigned> bucketIds = mesh.get_bucket_ids(field.get_rank(), selector);
    const unsigned numBuckets = bucketIds.size();
    ReductionTeamFunctor<Mesh, Field, ReductionOp> teamFunctor(mesh, field, bucketIds, initialValue);
    typename Field::value_type reduction;
    Kokkos::parallel_reduce(Kokkos::TeamPolicy<typename Mesh::MeshExecSpace>(numBuckets, Kokkos::AUTO), teamFunctor, reduction);
    return reduction;
}

template <typename Mesh, typename Field>
typename Field::value_type get_field_min(Mesh &mesh, Field field, const stk::mesh::Selector &selector)
{
    return get_field_reduction<Mesh, Field, MinFunctor<typename Field::value_type>>(mesh, field, selector, std::numeric_limits<typename Field::value_type>::max());
}

template <typename Mesh, typename Field>
typename Field::value_type get_field_max(Mesh &mesh, Field field, const stk::mesh::Selector &selector)
{
    return get_field_reduction<Mesh, Field, MaxFunctor<typename Field::value_type>>(mesh, field, selector, std::numeric_limits<typename Field::value_type>::min());
}

}


#endif /* PACKAGES_STK_STK_LEARNING_KOKKOS_REDUCTIONS_H_ */
