#pragma once

class QDialog;

namespace kai::ui {

// Moldura própria do Kai para TODO diálogo (QDialog, inclusive QMessageBox e
// QInputDialog): sem a decoração do sistema, com a mesma barra de título da
// janela principal (logo, título e botão fechar), cantos pelo raio da
// preferência do usuário (cantos nativos do DWM no Windows 11, máscara nos
// demais), arraste pela barra e redimensionamento pelas bordas.
//
// Funciona sobre o diálogo como ele é: o FramelessWindowHint é aplicado no polish
// (antes do primeiro show) e, no show, a barra de título (um filho sobreposto) é
// montada e a margem de cima do layout cresce a altura dela — serve a qualquer
// layout, sem mexer no código de cada diálogo.
//
// Instala o filtro no QApplication (idempotente). Deve ser chamada depois de
// criar a QApplication e só no modo gráfico.
void installDialogFrames();

// Veste um diálogo agora (o filtro de aplicação chama isto no polish). Não faz
// nada se ele já tem moldura, não é uma janela de topo do tipo diálogo ou já está
// visível.
void applyDialogFrame(QDialog *dialog);

bool hasDialogFrame(const QDialog *dialog);

} // namespace kai::ui
