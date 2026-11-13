// =============================================================================
//  sim/InterpreterC.inl - 1.50.1 : ce que le C et le C++ traduits demandent au moteur
// -----------------------------------------------------------------------------
//  Inclus DANS la classe Runner (Interpreter.cpp) : des fonctions du dialecte que
//  seule la traduction des scripts C et C++ emploie (hmi/HmiScriptC.cpp). Un nom
//  C qui les croiserait est renomme par la traduction.
//
//  L'ARITHMETIQUE DES POINTEURS DANS UN TABLEAU. Un pointeur du dialecte (POINTER
//  TO T) vise un objet (une case est un objet a part). Un pointeur pris sur une
//  case (CPTR_AT(t, i), le &t[i] et le t du C) garde aussi son tableau (Obj::map)
//  et le rang de la case (Obj::intKey) : il peut avancer et reculer dans le
//  tableau (CPTR_ADD), deux pointeurs du meme tableau se soustraient (CPTR_DIFF)
//  et se comparent (CPTR_EQ). Un pas hors du tableau (au-dela de la case qui suit
//  la derniere, que le C permet de viser sans la lire) est une erreur, a la ligne ;
//  lire ou ecrire la case qui suit la derniere aussi (follow).
//
//  LE TAS (new, delete, malloc, free). CNEW('T') cree un T, CNEW('T', n) un
//  tableau de n T (un pointeur sur sa premiere case) ; l'objet est garde avec les
//  variables du script (RunLimits::locals : il vit d'une execution a l'autre,
//  comme en C) jusqu'a CDELETE(p). Un objet libere est marque : le lire par un
//  pointeur, ou le liberer deux fois, est une erreur claire, a la ligne.
// =============================================================================

    // Un objet libere par CDELETE : marque (son champ `callee` ne sert qu'aux rappels).
    static constexpr const char* kCFreed = "\x01" "delete";
    static constexpr std::size_t kCHeapMax = 100000;        // au-dela : une fuite, dite
    std::vector<ObjRef> cFreed_;                            // les derniers objets liberes (le message clair)
    std::int64_t       cHeapCount_{-1};                     // les objets vivants du tas (-1 : pas encore comptes)

    static bool cFreedObj(const Obj& o) { return o.callee == kCFreed; }

    // Un pointeur neuf vers la case `flat` de `array` (la case qui suit la derniere : sans cible).
    RV cPointerAt(const ObjRef& array, std::int64_t flat) {
        auto t = std::make_shared<TypeDesc>();
        t->kind = TypeDesc::Kind::Pointer;
        t->element = array->type ? array->type->element : nullptr;
        auto o = std::make_shared<Obj>();
        o->type = t;
        o->bound = true;
        o->map = array;
        o->intKey = flat;
        if (flat >= 0 && flat < static_cast<std::int64_t>(array->items.size())) {
            o->target = array->items[static_cast<std::size_t>(flat)];
            if (!t->element && array->items[static_cast<std::size_t>(flat)]) t->element = array->items[static_cast<std::size_t>(flat)]->type;
        }
        return RV{{}, o};
    }

    // Un pointeur vers la case `index` d'un tableau de l'environnement (une variable IHM) : par son nom.
    RV cEnvPointer(const std::string& base, std::int64_t index, const TypeRef& element) {
        auto t = std::make_shared<TypeDesc>();
        t->kind = TypeDesc::Kind::Pointer;
        t->element = element;
        auto o = std::make_shared<Obj>();
        o->type = t;
        o->bound = true;
        o->textKey = base;
        o->intKey = index;
        o->targetName = base + "[" + std::to_string(index) + "]";
        return RV{{}, o};
    }

    core::Result<std::int64_t> cInteger(const Expr& a, const char* what) {
        auto v = evaluate(a);
        if (!v) return core::Err<core::Error>(v.error());
        if (!isInteger(v->type()) && v->type() != Type::Bool)
            return dialectError(std::string(what) + " : un entier est attendu, pas un " + std::string(toString(v->type())));
        return v->asInteger();
    }

    core::Result<ObjRef> cPointerArg(const Expr& a, const char* what) {
        auto v = rich(a);
        if (!v) return core::Err<core::Error>(v.error());
        if (!v->o || !isIndirect(v->o)) return dialectError(std::string(what) + " : un pointeur est attendu");
        return v->o;
    }

    static std::string cArrayWord(const Obj& array) {
        return "le tableau de " + std::to_string(array.items.size()) + " case" + (array.items.size() > 1 ? "s" : "");
    }

    // p avance de n cases (n < 0 : recule).
    core::Result<RV> cAdvance(const ObjRef& p, std::int64_t n) {
        if (!p->bound) return dialectError("arithm\xC3\xA9tique sur un pointeur nul (NULL)");
        if (auto array = p->map.lock(); array && array->type && array->type->kind == TypeDesc::Kind::Array) {
            if (cFreedObj(*array)) return dialectError("pointeur vers un tableau lib\xC3\xA9r\xC3\xA9 (delete) : il ne s'emploie plus");
            const std::int64_t to = p->intKey + n;
            const auto count = static_cast<std::int64_t>(array->items.size());
            if (to < 0 || to > count)
                return dialectError("pointeur hors du tableau : la case " + std::to_string(to) + " (" + cArrayWord(*array) + ", de 0 \xC3\xA0 "
                                    + std::to_string(count - 1) + ")");
            RV out = cPointerAt(array, to);
            if (p->type && p->type->element) {
                auto t = std::make_shared<TypeDesc>(*out.o->type);
                t->element = p->type->element;
                out.o->type = t;
            }
            return out;
        }
        if (!p->textKey.empty()) return cEnvPointer(p->textKey, p->intKey + n, p->type ? p->type->element : nullptr);
        if (n == 0) return RV{{}, p};
        return dialectError("arithm\xC3\xA9tique sur un pointeur qui ne vise pas une case de tableau (p = &x : *p seulement)");
    }

    // ---- les fonctions ----------------------------------------------------------
    std::optional<core::Result<RV>> nativesC(const std::string& u, const Expr& e) {
        if (!dialect_ || u.size() < 4 || u[0] != 'C') return std::nullopt;
        if (u != "CPTR_AT" && u != "CPTR_ADD" && u != "CPTR_DIFF" && u != "CPTR_EQ" && u != "CNEW" && u != "CDELETE") return std::nullopt;
        const auto argc = e.arguments.size();
        const auto bad = [&](const char* sig) { return core::Result<RV>(dialectError(std::string("mauvais nombre d'arguments : ") + sig)); };
        // CPTR_AT(t, i) : un pointeur sur la case i du tableau t (son indice ; un tableau a N dimensions : le rang a plat).
        if (u == "CPTR_AT") {
            if (argc != 2) return bad("CPTR_AT(tableau, indice)");
            const Expr& a = *e.arguments[0].second;
            auto i = cInteger(*e.arguments[1].second, "CPTR_AT");
            if (!i) return core::Result<RV>(core::Err<core::Error>(i.error()));
            ObjRef array;
            if (isDesignator(a)) {
                auto where = locate(a, false);
                if (!where) return core::Result<RV>(core::Err<core::Error>(where.error()));
                Place p = *where;
                while (p.obj && isIndirect(p.obj)) {
                    auto t = follow(p.obj);
                    if (!t) return core::Result<RV>(core::Err<core::Error>(t.error()));
                    p = *t;
                }
                if (!p.obj) {
                    // une variable IHM (un tableau de l'environnement) : la case par son nom
                    const std::string text = env_.declaredType(p.name);
                    TypeRef element;
                    if (!text.empty())
                        if (auto t = typeFromText(text); t && *t && (*t)->kind == TypeDesc::Kind::Array) element = (*t)->element;
                    return core::Result<RV>(cEnvPointer(env_.canonicalName(p.name), *i, element));
                }
                array = p.obj;
            } else {
                auto v = rich(a);
                if (!v) return core::Result<RV>(core::Err<core::Error>(v.error()));
                array = v->o;
            }
            if (!array || !array->type || array->type->kind != TypeDesc::Kind::Array)
                return core::Result<RV>(dialectError("CPTR_AT : un tableau est attendu"));
            std::int64_t flat = *i;
            if (array->type->bounds.size() == 1) flat -= array->type->bounds.front().first;
            const auto count = static_cast<std::int64_t>(array->items.size());
            if (flat < 0 || flat > count)
                return core::Result<RV>(dialectError("pointeur hors du tableau : la case " + std::to_string(*i) + " (" + cArrayWord(*array) + ")"));
            return core::Result<RV>(cPointerAt(array, flat));
        }
        // CPTR_ADD(p, n) : p + n.
        if (u == "CPTR_ADD") {
            if (argc != 2) return bad("CPTR_ADD(pointeur, n)");
            auto p = cPointerArg(*e.arguments[0].second, "CPTR_ADD");
            if (!p) return core::Result<RV>(core::Err<core::Error>(p.error()));
            auto n = cInteger(*e.arguments[1].second, "CPTR_ADD");
            if (!n) return core::Result<RV>(core::Err<core::Error>(n.error()));
            return cAdvance(*p, *n);
        }
        // CPTR_DIFF(p, q) : p - q, deux pointeurs du meme tableau (un nombre de cases).
        if (u == "CPTR_DIFF" || u == "CPTR_EQ") {
            if (argc != 2) return bad(u == "CPTR_EQ" ? "CPTR_EQ(p, q)" : "CPTR_DIFF(p, q)");
            auto p = cPointerArg(*e.arguments[0].second, u.c_str());
            if (!p) return core::Result<RV>(core::Err<core::Error>(p.error()));
            auto q = cPointerArg(*e.arguments[1].second, u.c_str());
            if (!q) return core::Result<RV>(core::Err<core::Error>(q.error()));
            const Obj& a = **p;
            const Obj& b = **q;
            const auto aa = a.map.lock(), ba = b.map.lock();
            if (u == "CPTR_EQ") {
                if (!a.bound || !b.bound) return core::Result<RV>(RV{Value::boolean(a.bound == b.bound), nullptr});
                if (aa || ba) return core::Result<RV>(RV{Value::boolean(aa == ba && a.intKey == b.intKey), nullptr});
                return core::Result<RV>(RV{Value::boolean(deepEquals(a, b)), nullptr});
            }
            if (!a.bound || !b.bound) return core::Result<RV>(dialectError("p - q : un des pointeurs est nul (NULL)"));
            if (!a.textKey.empty() && upperOf(a.textKey) == upperOf(b.textKey))
                return core::Result<RV>(RV{Value::integer(Type::DInt, a.intKey - b.intKey), nullptr});
            if (!aa || aa != ba) return core::Result<RV>(dialectError("p - q : les deux pointeurs ne visent pas le m\xC3\xAAme tableau"));
            return core::Result<RV>(RV{Value::integer(Type::DInt, a.intKey - b.intKey), nullptr});
        }
        Locals* store = limits_.locals ? limits_.locals : &ownLocals_;
        if (cHeapCount_ < 0) {
            cHeapCount_ = 0;
            for (const auto& [name, o] : store->all()) cHeapCount_ += name.rfind("__CHEAP_", 0) == 0;
        }
        const auto key = [](const Obj* o) {
            char buf[40];
            std::snprintf(buf, sizeof buf, "__CHEAP_%llx", static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(o)));
            return std::string(buf);
        };
        // CNEW('T') : un T neuf ; CNEW('T', n) : un tableau de n T, un pointeur sur sa premiere case.
        if (u == "CNEW") {
            if (argc != 1 && argc != 2) return bad("CNEW('type'[, nombre])");
            auto tv = evaluate(*e.arguments[0].second);
            if (!tv) return core::Result<RV>(core::Err<core::Error>(tv.error()));
            if (tv->type() != Type::String) return core::Result<RV>(dialectError("CNEW : le type s'\xC3\xA9" "crit en texte"));
            std::int64_t count = -1;
            if (argc == 2) {
                auto n = cInteger(*e.arguments[1].second, "new T[n]");
                if (!n) return core::Result<RV>(core::Err<core::Error>(n.error()));
                count = *n;
                if (count < 0) return core::Result<RV>(dialectError("new T[n] : n est n\xC3\xA9gatif (" + std::to_string(count) + ")"));
                if (count > 1000000) return core::Result<RV>(dialectError("new T[n] : " + std::to_string(count) + " cases, c'est trop (1000000 au plus)"));
            }
            if (cHeapCount_ >= static_cast<std::int64_t>(kCHeapMax))
                return core::Result<RV>(dialectError("le tas est plein (" + std::to_string(kCHeapMax) + " objets cr\xC3\xA9\xC3\xA9s par new et jamais lib\xC3\xA9r\xC3\xA9s : une fuite ? delete)"));
            auto t = typeFromText(tv->asString());
            if (!t) return core::Result<RV>(core::Err<core::Error>(t.error()));
            TypeRef type = *t;
            if (count >= 0) {
                auto a = std::make_shared<TypeDesc>();
                a->kind = TypeDesc::Kind::Array;
                a->bounds = {{0, std::max<std::int64_t>(count, 1) - 1}};
                a->element = type;
                type = a;
            }
            auto obj = instantiate(type);
            if (!obj) return core::Result<RV>(core::Err<core::Error>(obj.error()));
            if (count == 0) (*obj)->items.clear();           // new T[0] : un tableau vide (un pointeur sans case)
            store->set(key(obj->get()), *obj);
            ++cHeapCount_;
            if (count >= 0) return core::Result<RV>(cPointerAt(*obj, 0));
            auto pt = std::make_shared<TypeDesc>();
            pt->kind = TypeDesc::Kind::Pointer;
            pt->element = (*obj)->type;
            auto p = std::make_shared<Obj>();
            p->type = pt;
            p->bound = true;
            p->target = *obj;
            return core::Result<RV>(RV{{}, p});
        }
        // CDELETE(p) : libere l'objet ; CDELETE(p, TRUE) : un tableau (delete[]). NULL : rien.
        if (argc != 1 && argc != 2) return bad("CDELETE(pointeur[, tableau])");
        auto p = cPointerArg(*e.arguments[0].second, "delete");
        if (!p) return core::Result<RV>(core::Err<core::Error>(p.error()));
        bool asArray = false;
        if (argc == 2) {
            auto f = evaluate(*e.arguments[1].second);
            if (!f) return core::Result<RV>(core::Err<core::Error>(f.error()));
            asArray = f->isTruthy();
        }
        const Obj& ptr = **p;
        if (!ptr.bound) return core::Result<RV>(RV{Value::boolean(false), nullptr});
        ObjRef victim = ptr.map.lock();
        if (victim && (!victim->type || victim->type->kind != TypeDesc::Kind::Array)) victim.reset();
        const bool isArray = victim != nullptr;
        if (!victim) victim = ptr.target.lock();
        if (!victim) return core::Result<RV>(dialectError("delete : l'objet vis\xC3\xA9 n'existe plus (d\xC3\xA9j\xC3\xA0 lib\xC3\xA9r\xC3\xA9 ?)"));
        if (cFreedObj(*victim)) return core::Result<RV>(dialectError("delete deux fois : cet objet est d\xC3\xA9j\xC3\xA0 lib\xC3\xA9r\xC3\xA9"));
        if (isArray && ptr.intKey != 0)
            return core::Result<RV>(dialectError("delete : le pointeur ne vise pas le d\xC3\xA9" "but du tableau cr\xC3\xA9\xC3\xA9 par new (case " + std::to_string(ptr.intKey) + ")"));
        const std::string k = key(victim.get());
        if (!store->find(k)) return core::Result<RV>(dialectError("delete d'un objet qui n'a pas \xC3\xA9t\xC3\xA9 cr\xC3\xA9\xC3\xA9 par new (une variable du script)"));
        if (isArray != asArray && argc == 2)
            return core::Result<RV>(dialectError(isArray ? "delete sur un tableau cr\xC3\xA9\xC3\xA9 par new T[n] : \xC3\xA9" "cris delete[] p"
                                                         : "delete[] sur un objet cr\xC3\xA9\xC3\xA9 par new T (pas un tableau) : \xC3\xA9" "cris delete p"));
        store->erase(k);
        --cHeapCount_;
        // Marque (et ses cases) : un pointeur qui le vise encore le dira ; les derniers sont gardes pour cela.
        victim->callee = kCFreed;
        for (auto& item : victim->items)
            if (item) item->callee = kCFreed;
        cFreed_.push_back(victim);
        if (cFreed_.size() > 4096) cFreed_.erase(cFreed_.begin(), cFreed_.begin() + 1024);
        return core::Result<RV>(RV{Value::boolean(true), nullptr});
    }
