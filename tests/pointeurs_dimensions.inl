// =============================================================================
//  tests/pointeurs_dimensions.inl - 1.50.1 : LE SELECTEUR DE TYPES, la forme
//  << Tableau a N dimensions >> et + Dimension (inclus par pointeurs_test.cpp)
// =============================================================================
namespace {

void leSelecteurDimensions() {
    std::printf("-- 1.50.1 : le selecteur de types - Tableau a N dimensions, + Dimension\n");
    namespace tr = typereg;
    auto doc = std::make_shared<Document>();
    menu::MenuManager mm{menu::MenuFactory{}};
    app::HmiTypePicker::Spec spec;
    spec.field = "Type de Cube";
    spec.current = "BOOL";
    spec.use = tr::UseVariable;
    spec.doc = doc;
    mm.ShowDialog(std::make_unique<app::HmiTypePicker>(std::move(spec)), [](const menu::DialogResult&) {});
    mm.applyPending();
    auto* picker = dynamic_cast<app::HmiTypePicker*>(mm.top());
    check(picker != nullptr, "le selecteur s'ouvre (une variable IHM)");
    if (!picker) return;
    check(picker->result() == "BOOL" && picker->dimensions() == 0, "BOOL choisi d'office, pas un tableau : " + picker->result());
    // + Dimension depuis Simple : un tableau a une dimension, puis deux
    picker->addDimension();
    check(picker->result() == "ARRAY[0..9, 0..9] OF BOOL" && picker->dimensions() == 2,
          "+ Dimension sur une valeur simple : un tableau a deux dimensions (" + picker->result() + ")");
    picker->addDimension();
    check(picker->result() == "ARRAY[0..9, 0..9, 0..9] OF BOOL" && picker->dimensions() == 3 && picker->resultProblem().empty(),
          "+ Dimension : trois (" + picker->result() + " " + picker->resultProblem() + ")");
    picker->setArray(true, "0..3, 0..7, 0..15");
    check(picker->result() == "ARRAY[0..3, 0..7, 0..15] OF BOOL" && picker->resultProblem().empty(),
          "les bornes ecrites : " + picker->result() + " " + picker->resultProblem());
    picker->addDimension();
    check(picker->result() == "ARRAY[0..3, 0..7, 0..15, 0..15] OF BOOL" && picker->dimensions() == 4 && picker->resultProblem().empty(),
          "quatre dimensions, pour une variable IHM : " + picker->result());
    picker->removeDimension();
    picker->removeDimension();
    picker->removeDimension();
    check(picker->result() == "ARRAY[0..3] OF BOOL" && picker->dimensions() == 1, "trois de moins : la forme Tableau (" + picker->result() + ")");
    check(picker->setForm("Tableau \xC3\xA0 N dimensions") && picker->setForm("Tableau 2D") && picker->setForm("tableau2d"),
          "la forme se choisit par son libelle, l'ancien et sa cle");
    // un grand tableau ecrit dans la recherche : accepte (dans la memoire de l'IHM)
    picker->setSearch("array[0..999, 0..9999] of bool");
    check(picker->result() == "ARRAY[0..999, 0..9999] OF BOOL" && picker->resultProblem().empty(),
          "un grand tableau ecrit : " + picker->result() + " " + picker->resultProblem());
    picker->finish(false);
}

} // namespace
