#include "dbt/qjit/qcg.h"

namespace dbt::qcg
{

TBlock *QCodegen::Generate(qir::Region *r)
{
	QEmit qe(r);
	QCodegen qc(r, &qe);

	// translate
	return nullptr;
}

} // namespace dbt::qcg
